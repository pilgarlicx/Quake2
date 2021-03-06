/*-----------------------------------------------------------------------------
	PKWare .zip file loader
-----------------------------------------------------------------------------*/

#include "../../lib/zlib/zlib.h"

// type defines (change ??)
typedef unsigned char   uch;		// 1-byte unsigned
typedef unsigned short  ush;		// 2-byte unsigned
typedef unsigned long   ulg;		// 4-byte unsigned


#define CENTRAL_HDR_MAGIC	BYTES4('P','K',1,2)
#define LOCAL_HDR_MAGIC		BYTES4('P','K',3,4)
#define END_HDR_MAGIC		BYTES4('P','K',5,6)


//#define ZLIB_DEBUG		1


#if _MSC_VER
#pragma pack(push,1)
#endif

struct GCC_PACK localFileHdr_t
{
	// LOCAL_HDR_MAGIC
	uch		versionNeededToExtract[2];
	ush		generalPurposeBitFlag;
	ush		compressionMethod;
	ulg		lastModDosDatetime;
	ulg		crc32;
	ulg		csize;
	ulg		ucsize;
	ush		filenameLength;
	ush		extraFieldLength;
};

struct GCC_PACK endOfCentralDir_t
{
	// END_HDR_MAGIC
	ush		diskNumber, cdirDiskNumber;
	ush		numEntries, cdirNumEntries;
	ulg		cdirSize;
	ulg		cdirOffset;
	ush		commentSize;
};

struct GCC_PACK cdirFileHdr_t
{
	// CENTRAL_HDR_MAGIC
	uch		versionMadeBy[2];
	uch		versionNeededToExtract[2];
	ush		generalPurposeBitFlag;
	ush		compressionMethod;
	ulg		lastModDosDatetime;
	ulg		crc32;
	ulg		csize;
	ulg		ucsize;
	ush		filenameLength;
	ush		extraFieldLength;
	ush		fileCommentLength;
	ush		diskNumberStart;
	ush		internalFileAttributes;
	ulg		externalFileAttributes;
	ulg		relativeOffsetLocalHeader;
};

#if _MSC_VER
#pragma pack(pop)
#endif


class CFileContainerZip : public CFileContainerArc
{
protected:
	// file entry
	class CFileInfo : public CStringItem
	{
	public:
		unsigned pos;				// position inside archive
		unsigned size;				// uncompressed size
		unsigned csize;				// compressed size
		unsigned crc32;
		byte	method;				// 0=store, 8=deflate
	};
	// CFile
	class CFilePak : public CFile
	{
	protected:
		byte	buf[65536];			// buffer for file reading
	public:
		FILE	*file;				// opened file
		const CFileInfo &info;		// pointer to file information
		unsigned readPos;			// number of bytes, sent out (uncompressed)
		unsigned restRead;			// number of compressed bytes left
		unsigned crc;
		z_stream s;					// zlib data
		CFilePak(const CFileInfo &AInfo, FILE *AFile)
		:	info(AInfo)
		,	file(AFile)
		,	restRead(AInfo.csize)
		{
			if (info.method != Z_DEFLATED) return;
			// init decompression
#if !ZLIB_DEBUG
			inflateInit2(&s, -MAX_WBITS);
#else
			if (inflateInit2(&s, -MAX_WBITS) != Z_OK)
				// Error happens in following situations only:
				// 1. zlib major version mismatch (possible only with dynamically linked zlib in a future)
				// 2. invalid arguments (here: all correct)
				// 3. memory allocation fail - our memory manager NEVER returns NULL - it
				//    will throw appError() in such case (so, impossible case)
				appError("ZLIB: inflateInit2() error");
#endif
		}
		~CFilePak()
		{
			// delete deflate object
			if (info.method == Z_DEFLATED)
				inflateEnd(&s);
			// close file
			if (file)
				fclose(file);
			// verify crc (when file completely read)
			if (readPos >= info.size && crc != info.crc32)
				appWPrintf("zip \"%s\": \"%s\" have wrong CRC\n", Owner->name, *Name);
#if ZLIB_DEBUG
			else
				appPrintf("file %s: crc = %X\n", *Name, crc);
#endif
		}
		int Read(void *Buffer, int Size)
		{
			if (!file)
				return 0;
			int len = info.size - readPos;
			if (len > Size) len = Size;
			if (info.method == Z_DEFLATED)
			{
				s.avail_out = len;
				s.next_out  = (byte*)Buffer;
				while (s.avail_out > 0)
				{
					if (!s.avail_in && restRead)
					{
						// buffer empty - fill it
						int compRead = min(restRead, sizeof(buf));
						if (fread(buf, compRead, 1, file) != 1)
							return 0; // cannot read ...
						s.avail_in += compRead;
						restRead   -= compRead;
						s.next_in = buf;
					}
					// decompress data
					int ret = inflate(&s, Z_SYNC_FLUSH);
					// check for error
					if (ret == Z_OK)	// get next data
						continue;
					if (ret == Z_STREAM_END)
					{
						if (s.avail_in + s.avail_out + restRead)
						{
							appWPrintf("zip \"%s\": \"%s\": unexpected eof\n", Owner->name, *Name);
							readPos = info.size;	// do not read next time
						}
						break;
					}
					// here: error occured
#if ZLIB_DEBUG
					appWPrintf("ZLIB: inflate() err=%d\n", ret);
#endif
					return 0;
				}
			}
			else
			{
				// read stored file
				// here: readPos+restRead == info.csize == info.size
				if (fread(Buffer, len, 1, file) != 1)
					return 0;
				restRead -= len;
			}
			readPos += len;
			// update crc32
			crc = crc32(crc, (byte*)Buffer, len);
			return len;
		}
		int GetSize()
		{
			return info.size;
		}
		bool Eof()
		{
			return readPos >= info.size;
		}
	};
	const char *GetType()
	{
		return "zip";
	}
	// function for file opening
	CFile *OpenLocalFile(const FFileInfo &Info)
	{
		const CFileInfo &info = static_cast<const CFileInfo&>(Info);
		FILE *f = fopen(name, "rb");
		if (!f)						// cannot open archive
			return NULL;
		// rewind to file start
		fseek(f, info.pos, SEEK_SET);
		// verify/skip header
		ulg signature;
		if (fread(&signature, 4, 1, f) != 1 ||
			signature != LOCAL_HDR_MAGIC)
			return NULL;
		localFileHdr_t hdr;
		if (fread(&hdr, sizeof(hdr), 1, f) != 1)
			return NULL;
		fseek(f, hdr.filenameLength + hdr.extraFieldLength, SEEK_CUR);
		// create CFile struct
		CFilePak *File = new CFilePak(info, f);
		return File;
	}
public:
	static CFileContainer *Create(const char *filename, FILE *f)
	{
#if ZLIB_DEBUG
		// can disable this code to allow zip files, attached to another file (sfx archives etc)
		unsigned signature;
		if (fread(&signature, 4, 1, f) != 1 || signature != LOCAL_HDR_MAGIC)
			return NULL;
#endif
		// rewind to the archive end
		if (fseek(f, 0, SEEK_END))
			return NULL;
		// find end of central directory record (by signature)
		unsigned pos;
		char buf[512];
		if ((pos = ftell(f)) < 22)	// file too small to be a zip-archive
			return NULL;
		// here: 18 = sizeof(ecdir_rec_ehdr) - sizeof(signature)
		if (pos < sizeof(buf) - 18)
			pos = 0;				// small file
		else
			pos -= sizeof(buf) - 18;

		// try to fill buf 128 times (64K total - max size of central arc comment)
		bool found = false;
		for (int i = 0; i < 128; i++)
		{
			if (fseek(f, pos, SEEK_SET)) return false;
			unsigned readBytes = fread(buf, 1, sizeof(buf), f);
			if (!readBytes) return NULL;
			for (char *buf_ptr = buf + readBytes - 4; buf_ptr >= buf; buf_ptr--)
				if (*(unsigned*)buf_ptr == END_HDR_MAGIC)
				{
					pos += buf_ptr - buf + 4; // 4 - sizeof(END_HDR_MAGIC)
					found = true;
					break;
				}
			if (found) break;
			pos -= sizeof(buf) - 4;	// make a 4 bytes overlap between readings
			if (pos < 0)			// reached file start
				return NULL;
		}
		if (!found) return NULL;

		// get pointer to a central directory
		// read end of central directory record
		endOfCentralDir_t endCDir;
		fseek(f, pos, SEEK_SET);
		if (fread(&endCDir, sizeof(endCDir), 1, f) != 1) return NULL;
		int posDelta = pos - (endCDir.cdirOffset+endCDir.cdirSize+4);	// +4-sizeof(CENTRAL_HDR_MAGIC); != 0 for SFX archives
		if (posDelta < 0)
			return NULL;
		// seek to a central directory
		if (fseek(f, endCDir.cdirOffset+posDelta, SEEK_SET)) return NULL;

		CFileContainerZip *Zip = new (filename) CFileContainerZip;
		// WARNING: when wrong archive loaded, function will return NULL, but will not "delete Zip"
		while (true)
		{
			unsigned signature;
			if (fread(&signature, 4, 1, f) != 1)
				return NULL;		// cannot read signature
			if (signature == END_HDR_MAGIC)
				break;				// end of central directory structure - finish
			if (signature != CENTRAL_HDR_MAGIC)
				return NULL;		// MUST be a central directory file header

			cdirFileHdr_t hdr;									// central directory record
			if (fread(&hdr, sizeof(hdr), 1, f) != 1)
				return NULL;

			// read file name
			unsigned nameLen = hdr.filenameLength;
			if (nameLen >= sizeof(buf))
			{
				appWPrintf("CFileContainerZip::Create: %s contains file with too long name\n", filename);
				return NULL;
			}
			if (fread(&buf[0], nameLen, 1, f) != 1) return false;
			buf[nameLen] = 0;
			appCopyFilename(buf, buf);
			// verify for supported compression method
			if (hdr.compressionMethod != Z_DEFLATED && hdr.compressionMethod != 0)
			{
				appWPrintf("CFileContainerZip::Create: %s have file %s with unknown compression method %d\n",
					filename, buf, hdr.compressionMethod);
				return NULL;
			}
			// seek to next file info
			if (fseek(f, hdr.extraFieldLength + hdr.fileCommentLength, SEEK_CUR))
				return NULL;
//			appPrintf("%10d < %10d  [%1d]  %s\n", hdr.csize, hdr.ucsize, hdr.compressionMethod, buf);

			char *name = strrchr(buf, '/');
			// find/create directory
			FDirInfo *Dir;
			if (name)
			{
				*name++ = 0;
				Dir = Zip->FindDir(buf, true);
			}
			else
			{
				name = buf;
				Dir = &Zip->Root;
			}
			// dirs have size=0, method=0, last_name_char='/'
			if (!name[0])			// add files only
				continue;
			// add file to dir
			CStringItem *ins;
			CFileInfo *info = static_cast<CFileInfo*>(Dir->Files.Find(name, &ins));
			if (!info)
			{
				// should always pass
				info = new (name, Zip->mem) CFileInfo;
				Dir->Files.InsertAfter(info, ins);
				info->pos    = hdr.relativeOffsetLocalHeader + posDelta;
				info->size   = hdr.ucsize;
				info->csize  = hdr.csize;
				info->crc32  = hdr.crc32;
				info->method = hdr.compressionMethod;

				Zip->numFiles++;
			}
		}

		return Zip;
	}
};
