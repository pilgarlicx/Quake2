/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "server.h"

/*
=============================================================================

Encode a client frame onto the network channel

=============================================================================
*/

/*
=============
SV_EmitPacketEntities

Writes a delta update of an entity_state_t list to the message.
=============
*/
static void SV_EmitPacketEntities (client_frame_t *from, client_frame_t *to, sizebuf_t *msg)
{
	int		oldindex, newindex;
	int		from_num_entities;

	MSG_WriteByte (msg, svc_packetentities);

	from_num_entities = from ? from->num_entities : 0;

	newindex = oldindex = 0;
	while (newindex < to->num_entities || oldindex < from_num_entities)
	{
		int		oldnum, newnum;
		entity_state_t *oldent, *newent;

		if (newindex >= to->num_entities)
			newnum = BIG_NUMBER;
		else
		{
			newent = &svs.client_entities[(to->first_entity + newindex) % svs.num_client_entities];
			newnum = newent->number;
		}

		if (oldindex >= from_num_entities)
			oldnum = BIG_NUMBER;
		else
		{
			oldent = &svs.client_entities[(from->first_entity + oldindex) % svs.num_client_entities];
			oldnum = oldent->number;
		}

		if (newnum == oldnum)
		{
			// delta update from old position
			// note that players are always 'newentities', this updates their oldorigin always and prevents warping
			// NOTE: it's impossible to get newindex AND oldindex both overflowed (> num_entities), because of while() condition
			MSG_WriteDeltaEntity (oldent, newent, msg, false, newent->number <= maxclients->integer);
			oldindex++;
			newindex++;
			continue;
		}

		if (newnum < oldnum)
		{
			// this is a new entity, send it from the baseline
			MSG_WriteDeltaEntity (&sv.baselines[newnum], newent, msg, true, true);
			newindex++;
			continue;
		}

		if (newnum > oldnum)
		{
			int		bits;

			// the old entity isn't present in the new message
			bits = U_REMOVE;
			if (oldnum >= 256)
				bits |= U_NUMBER16 | U_MOREBITS1;

			MSG_WriteByte (msg,	bits & 255);
			if (bits & 0x0000FF00)
				MSG_WriteByte (msg,	(bits>>8) & 255);

			if (bits & U_NUMBER16)
				MSG_WriteShort (msg, oldnum);
			else
				MSG_WriteByte (msg, oldnum);

			oldindex++;
			continue;
		}
	}

	MSG_WriteShort (msg, 0);	// end of packetentities; corresponds to bits(byte)=0,entNum(byte)=0
}



/*
=============
SV_WritePlayerstateToClient

=============
*/
void SV_WritePlayerstateToClient (client_frame_t *from, client_frame_t *to, sizebuf_t *msg)
{
	int				i;
	int				pflags;
	player_state_t	*ps, *ops;
	player_state_t	dummy;
	int				statbits;

	ps = &to->ps;
	if (!from)
	{
		memset (&dummy, 0, sizeof(dummy));
		ops = &dummy;
	}
	else
		ops = &from->ps;

	// determine what needs to be sent
	pflags = 0;

	if (ps->pmove.pm_type != ops->pmove.pm_type)
		pflags |= PS_M_TYPE;

	if (ps->pmove.origin[0] != ops->pmove.origin[0] ||
		ps->pmove.origin[1] != ops->pmove.origin[1] ||
		ps->pmove.origin[2] != ops->pmove.origin[2])
		pflags |= PS_M_ORIGIN;

	if (ps->pmove.velocity[0] != ops->pmove.velocity[0] ||
		ps->pmove.velocity[1] != ops->pmove.velocity[1] ||
		ps->pmove.velocity[2] != ops->pmove.velocity[2])
		pflags |= PS_M_VELOCITY;

	if (ps->pmove.pm_time != ops->pmove.pm_time)
		pflags |= PS_M_TIME;

	if (ps->pmove.pm_flags != ops->pmove.pm_flags)
		pflags |= PS_M_FLAGS;

	if (ps->pmove.gravity != ops->pmove.gravity)
		pflags |= PS_M_GRAVITY;

	if (ps->pmove.delta_angles[0] != ops->pmove.delta_angles[0] ||
		ps->pmove.delta_angles[1] != ops->pmove.delta_angles[1] ||
		ps->pmove.delta_angles[2] != ops->pmove.delta_angles[2])
		pflags |= PS_M_DELTA_ANGLES;


	if (ps->viewoffset[0] != ops->viewoffset[0] ||
		ps->viewoffset[1] != ops->viewoffset[1] ||
		ps->viewoffset[2] != ops->viewoffset[2])
		pflags |= PS_VIEWOFFSET;

	if (ps->viewangles[0] != ops->viewangles[0] ||
		ps->viewangles[1] != ops->viewangles[1] ||
		ps->viewangles[2] != ops->viewangles[2])
		pflags |= PS_VIEWANGLES;

	if (ps->kick_angles[0] != ops->kick_angles[0] ||
		ps->kick_angles[1] != ops->kick_angles[1] ||
		ps->kick_angles[2] != ops->kick_angles[2])
		pflags |= PS_KICKANGLES;

	if (ps->blend[0] != ops->blend[0] ||
		ps->blend[1] != ops->blend[1] ||
		ps->blend[2] != ops->blend[2] ||
		ps->blend[3] != ops->blend[3])
		pflags |= PS_BLEND;

	if (ps->fov != ops->fov)
		pflags |= PS_FOV;

	if (ps->rdflags != ops->rdflags)
		pflags |= PS_RDFLAGS;

	if (ps->gunframe != ops->gunframe)
		pflags |= PS_WEAPONFRAME;

	pflags |= PS_WEAPONINDEX;

	// write it
	MSG_WriteByte (msg, svc_playerinfo);
	MSG_WriteShort (msg, pflags);

	// write the pmove_state_t
	if (pflags & PS_M_TYPE)
		MSG_WriteByte (msg, ps->pmove.pm_type);

	if (pflags & PS_M_ORIGIN)
	{
		MSG_WriteShort (msg, ps->pmove.origin[0]);
		MSG_WriteShort (msg, ps->pmove.origin[1]);
		MSG_WriteShort (msg, ps->pmove.origin[2]);
	}

	if (pflags & PS_M_VELOCITY)
	{
		MSG_WriteShort (msg, ps->pmove.velocity[0]);
		MSG_WriteShort (msg, ps->pmove.velocity[1]);
		MSG_WriteShort (msg, ps->pmove.velocity[2]);
	}

	if (pflags & PS_M_TIME)
		MSG_WriteByte (msg, ps->pmove.pm_time);

	if (pflags & PS_M_FLAGS)
		MSG_WriteByte (msg, ps->pmove.pm_flags);

	if (pflags & PS_M_GRAVITY)
		MSG_WriteShort (msg, ps->pmove.gravity);

	if (pflags & PS_M_DELTA_ANGLES)
	{
		MSG_WriteShort (msg, ps->pmove.delta_angles[0]);
		MSG_WriteShort (msg, ps->pmove.delta_angles[1]);
		MSG_WriteShort (msg, ps->pmove.delta_angles[2]);
	}

	// write the rest of the player_state_t
	if (pflags & PS_VIEWOFFSET)
	{
		MSG_WriteChar (msg, Q_round (ps->viewoffset[0]*4));
		MSG_WriteChar (msg, Q_round (ps->viewoffset[1]*4));
		MSG_WriteChar (msg, Q_round (ps->viewoffset[2]*4));
	}

	if (pflags & PS_VIEWANGLES)
	{
		MSG_WriteAngle16 (msg, ps->viewangles[0]);
		MSG_WriteAngle16 (msg, ps->viewangles[1]);
		MSG_WriteAngle16 (msg, ps->viewangles[2]);
	}

	if (pflags & PS_KICKANGLES)
	{
		MSG_WriteChar (msg, Q_round (ps->kick_angles[0]*4));
		MSG_WriteChar (msg, Q_round (ps->kick_angles[1]*4));
		MSG_WriteChar (msg, Q_round (ps->kick_angles[2]*4));
	}

	if (pflags & PS_WEAPONINDEX)
	{
		MSG_WriteByte (msg, ps->gunindex);
	}

	if (pflags & PS_WEAPONFRAME)
	{
		MSG_WriteByte (msg, ps->gunframe);
		MSG_WriteChar (msg, Q_round (ps->gunoffset[0]*4));
		MSG_WriteChar (msg, Q_round (ps->gunoffset[1]*4));
		MSG_WriteChar (msg, Q_round (ps->gunoffset[2]*4));
		MSG_WriteChar (msg, Q_round (ps->gunangles[0]*4));
		MSG_WriteChar (msg, Q_round (ps->gunangles[1]*4));
		MSG_WriteChar (msg, Q_round (ps->gunangles[2]*4));
	}

	if (pflags & PS_BLEND)
	{
		MSG_WriteByte (msg, Q_round (ps->blend[0]*255));
		MSG_WriteByte (msg, Q_round (ps->blend[1]*255));
		MSG_WriteByte (msg, Q_round (ps->blend[2]*255));
		MSG_WriteByte (msg, Q_round (ps->blend[3]*255));
	}
	if (pflags & PS_FOV)
		MSG_WriteByte (msg, Q_round (ps->fov));
	if (pflags & PS_RDFLAGS)
		MSG_WriteByte (msg, ps->rdflags);

	// send stats
	statbits = 0;
	for (i = 0; i < MAX_STATS; i++)
		if (ps->stats[i] != ops->stats[i])
			statbits |= 1<<i;
	MSG_WriteLong (msg, statbits);
	for (i = 0; i < MAX_STATS; i++)
		if (statbits & (1<<i))
			MSG_WriteShort (msg, ps->stats[i]);
}


/*
==================
SV_WriteFrameToClient
==================
*/
void SV_WriteFrameToClient (client_t *client, sizebuf_t *msg)
{
	client_frame_t		*frame, *oldframe;
	int					lastframe;

//Com_Printf ("%i -> %i\n", client->lastframe, sv.framenum);
	// this is the frame we are creating
	frame = &client->frames[sv.framenum & UPDATE_MASK];

	if (client->lastframe <= 0)
	{	// client is asking for a retransmit
		oldframe = NULL;
		lastframe = -1;
	}
	else if (sv.framenum - client->lastframe >= (UPDATE_BACKUP - 3))
	{	// client hasn't gotten a good message through in a long time
//		Com_Printf ("%s: Delta request from out-of-date packet.\n", client->name);
		oldframe = NULL;
		lastframe = -1;
	}
	else
	{	// we have a valid message to delta from
		oldframe = &client->frames[client->lastframe & UPDATE_MASK];
		lastframe = client->lastframe;
	}

	MSG_WriteByte (msg, svc_frame);
	MSG_WriteLong (msg, sv.framenum);
	MSG_WriteLong (msg, lastframe);	// what we are delta'ing from
	MSG_WriteByte (msg, client->surpressCount);	// rate dropped packets
	client->surpressCount = 0;

	// send over the areabits
	MSG_WriteByte (msg, frame->areabytes);
	SZ_Write (msg, frame->areabits, frame->areabytes);

	// delta encode the playerstate
	SV_WritePlayerstateToClient (oldframe, frame, msg);

	// delta encode the entities
	SV_EmitPacketEntities (oldframe, frame, msg);
}


/*
=============================================================================

Build a client frame structure

=============================================================================
*/

byte		fatpvs[65536/8];	// 32767 is MAX_MAP_LEAFS

/*
============
SV_FatPVS

The client will interpolate the view position,
so we can't use a single PVS point
===========
*/
void SV_FatPVS (vec3_t org)
{
	int		leafs[64];
	int		i, j, count;
	int		longs;
	byte	*src;
	vec3_t	mins, maxs;

	for (i = 0; i < 3; i++)
	{
		mins[i] = org[i] - 8;		// box should be greater, when client in 3rd person ??
		maxs[i] = org[i] + 8;
	}

	count = CM_BoxLeafnums (mins, maxs, leafs, 64, NULL);
	if (count < 1)
		Com_FatalError ("SV_FatPVS: count < 1");
	longs = (CM_NumClusters()+31)>>5;

	// convert leafs to clusters
	for (i = 0; i < count; i++)
		leafs[i] = CM_LeafCluster (leafs[i]);

	memcpy (fatpvs, CM_ClusterPVS (leafs[0]), longs<<2);
	// or in all the other leaf bits
	for (i = 1; i < count; i++)
	{
		for (j = 0; j < i; j++)
			if (leafs[i] == leafs[j])
				break;
		if (j != i)
			continue;		// already have the cluster we want
		src = CM_ClusterPVS (leafs[i]);
		for (j = 0; j < longs; j++)
			((long *)fatpvs)[j] |= ((long *)src)[j];
	}
}


/*
=============
SV_BuildClientFrame

Decides which entities are going to be visible to the client, and
copies off the playerstat and areabits.
=============
*/
void SV_BuildClientFrame (client_t *client)
{
	int		e, i;
	vec3_t	org;
	edict_t	*ent, *cl_ent;
	client_frame_t	*frame;
	entity_state_t	*state;
	int		l, leafnum;
	int		clientarea, clientcluster;
	int		c_fullsend;
	byte	*clientphs, *bitvector;

	guard(SV_BuildClientFrame);

	cl_ent = client->edict;
	if (!cl_ent->client)
		return;		// not in game yet

	// this is the frame we are creating
	frame = &client->frames[sv.framenum & UPDATE_MASK];

	frame->senttime = svs.realtime; // save it for ping calc later

	// find the client's PVS
	for (i = 0; i < 3; i++)
		org[i] = cl_ent->client->ps.pmove.origin[i]*0.125f + cl_ent->client->ps.viewoffset[i];

	leafnum = CM_PointLeafnum (org);
	clientarea = CM_LeafArea (leafnum);
	clientcluster = CM_LeafCluster (leafnum);

	// calculate the visible areas
	frame->areabytes = CM_WriteAreaBits (frame->areabits, clientarea);

	// grab the current player_state_t
	frame->ps = cl_ent->client->ps;


	SV_FatPVS (org);
	clientphs = CM_ClusterPHS (clientcluster);

	// build up the list of visible entities
	frame->num_entities = 0;
	frame->first_entity = svs.next_client_entities;

	c_fullsend = 0;

	for (e = 1; e < ge->num_edicts; e++)
	{
		ent = EDICT_NUM(e);

		// ignore ents without visible models
		if (ent->svflags & SVF_NOCLIENT)
			continue;

		// ignore ents without visible models unless they have an effect
		if (!ent->s.modelindex && !ent->s.effects && !ent->s.sound && !ent->s.event)
			continue;

		// ignore if not touching a PV leaf
		if (ent != cl_ent)
		{
			// check area
			if (!CM_AreasConnected (clientarea, ent->areanum))
			{	// doors can legally straddle two areas, so
				// we may need to check another one
				if (!ent->areanum2 || !CM_AreasConnected (clientarea, ent->areanum2))
					continue;		// blocked by a door
			}

			// beams just check one point for PHS
			if (ent->s.renderfx & RF_BEAM)
			{
				l = ent->clusternums[0];
				if (!(clientphs[l >> 3] & (1 << (l & 7))))
					continue;
			}
			else
			{
				// FIXME: if an ent has a model and a sound, but isn't
				// in the PVS, only the PHS, clear the model
				if (ent->s.sound)
				{
					bitvector = fatpvs;	//clientphs;
				}
				else
					bitvector = fatpvs;

				if (ent->num_clusters == -1)
				{	// too many leafs for individual check, go by headnode
					if (!CM_HeadnodeVisible (ent->headnode, bitvector))
						continue;
					c_fullsend++;
				}
				else
				{	// check individual leafs
					for (i = 0; i < ent->num_clusters; i++)
					{
						l = ent->clusternums[i];
						if (bitvector[l >> 3] & (1 << (l&7)))
							break;
					}
					if (i == ent->num_clusters)
						continue;		// not visible
				}

				if (!ent->s.modelindex)
				{	// don't send sounds if they will be attenuated away
					vec3_t	delta;
					float	len;

					VectorSubtract (org, ent->s.origin, delta);
					len = VectorLength (delta);
					if (len > 400)
						continue;
				}
			}
		}

		// add it to the circular client_entities array
		state = &svs.client_entities[svs.next_client_entities%svs.num_client_entities];
		if (ent->s.number != e)
		{
			Com_DPrintf ("FIXING ENT->S.NUMBER!!!\n");
			ent->s.number = e;
		}
		*state = ent->s;

//		if (state->event) Com_Printf("Ent #%i -- sfx #%i\n",e,state->event);//!!!
		// disable new features for clients using old protocol
		if (!client->newprotocol)
		{
			int evt;

			evt = state->event;

			if (evt >= EV_FOOTSTEP0 && evt < EV_FOOTSTEP0 + MATERIAL_COUNT)
				evt = EV_FOOTSTEP;
			else if (evt >= EV_FALLSHORT0 && evt < EV_FALLSHORT0 + MATERIAL_COUNT)
				evt = EV_FALLSHORT;
			else if (evt >= EV_SPECTATOR0 && evt <= EV_CAMPER8)
				evt = EV_NONE;

			state->event = evt;
		}

		// don't mark players missiles as solid
		if (ent->owner == client->edict)
			state->solid = 0;

		svs.next_client_entities++;
		frame->num_entities++;
	}

	unguard;
}


/*
==================
SV_RecordDemoMessage

Save everything in the world out without deltas.
Used for recording footage for merged or assembled demos
==================
*/
void SV_RecordDemoMessage (void)
{
	int		i, len;
	entity_state_t	nostate;
	sizebuf_t buf;
	byte	buf_data[32768];

	if (!svs.demofile)
		return;

	memset (&nostate, 0, sizeof(nostate));
	SZ_Init (&buf, buf_data, sizeof(buf_data));

	// write a frame message that doesn't contain a player_state_t
	MSG_WriteByte (&buf, svc_frame);
	MSG_WriteLong (&buf, sv.framenum);

	MSG_WriteByte (&buf, svc_packetentities);

	for (i = 1; i < ge->num_edicts; i++)
	{
		edict_t		*ent;

		ent = EDICT_NUM(i);
		// ignore ents without visible models unless they have an effect
		if (ent->inuse && ent->s.number && !(ent->svflags & SVF_NOCLIENT) &&
			(ent->s.modelindex || ent->s.effects || ent->s.sound || ent->s.event))
			MSG_WriteDeltaEntity (&nostate, &ent->s, &buf, false, true);
	}

	MSG_WriteShort (&buf, 0);		// end of packetentities

	// now add the accumulated multicast information
	SZ_Write (&buf, svs.demo_multicast.data, svs.demo_multicast.cursize);
	SZ_Clear (&svs.demo_multicast);

	// now write the entire message to the file, prefixed by the length
	len = LittleLong (buf.cursize);
	fwrite (&len, 4, 1, svs.demofile);
	fwrite (buf.data, buf.cursize, 1, svs.demofile);
}