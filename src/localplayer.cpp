/*
Minetest
Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "localplayer.h"

#include "main.h" // For g_settings
#include "event.h"
#include "collision.h"
#include "gamedef.h"
#include "nodedef.h"
#include "settings.h"
#include "environment.h"
#include "map.h"
#include "util/numeric.h"

/*
	LocalPlayer
*/

LocalPlayer::LocalPlayer(IGameDef *gamedef):
	Player(gamedef),
	parent(0),
	isAttached(false),
	overridePosition(v3f(0,0,0)),
	last_position(v3f(0,0,0)),
	last_speed(v3f(0,0,0)),
	last_pitch(0),
	last_yaw(0),
	last_keyPressed(0),
	m_sneak_node(32767,32767,32767),
	m_sneak_node_exists(false),
	m_old_node_below(32767,32767,32767),
	m_old_node_below_type("air"),
	m_need_to_get_new_sneak_node(true),
	m_can_jump(false)
{
	// Initialize hp to 0, so that no hearts will be shown if server
	// doesn't support health points
	hp = 0;
}

LocalPlayer::~LocalPlayer()
{
}

void LocalPlayer::move(f32 dtime, ClientEnvironment *env, f32 pos_max_d,
		std::list<CollisionInfo> *collision_info)
{
}

void LocalPlayer::move(f32 dtime, ClientEnvironment *env, f32 pos_max_d)
{
	move(dtime, env, pos_max_d, NULL);
}

void LocalPlayer::applyControl(float dtime)
{
	// Clear stuff
	swimming_vertical = false;

	setPitch(control.pitch);
	setYaw(control.yaw);

	// Nullify speed and don't run positioning code if the player is attached
	if(isAttached)
	{
		setSpeed(v3f(0,0,0));
		return;
	}

	v3f move_direction = v3f(0,0,1);
	move_direction.rotateXZBy(getYaw());
	
	v3f speedH = v3f(0,0,0); // Horizontal (X, Z)
	v3f speedV = v3f(0,0,0); // Vertical (Y)
	
	bool fly_allowed = m_gamedef->checkLocalPrivilege("fly");
	bool fast_allowed = m_gamedef->checkLocalPrivilege("fast");

	bool free_move = fly_allowed && g_settings->getBool("free_move");
	bool fast_move = fast_allowed && g_settings->getBool("fast_move");
	// When aux1_descends is enabled the fast key is used to go down, so fast isn't possible
	bool fast_climb = fast_move && control.aux1 && !g_settings->getBool("aux1_descends");
	bool continuous_forward = g_settings->getBool("continuous_forward");

	// Whether superspeed mode is used or not
	bool superspeed = false;
	
	if(g_settings->getBool("always_fly_fast") && free_move && fast_move)
		superspeed = true;

	// Old descend control
	if(g_settings->getBool("aux1_descends"))
	{
		// If free movement and fast movement, always move fast
		if(free_move && fast_move)
			superspeed = true;
		
		// Auxiliary button 1 (E)
		if(control.aux1)
		{
			if(free_move)
			{
				// In free movement mode, aux1 descends
				if(fast_move)
					speedV.Y = -movement_speed_fast;
				else
					speedV.Y = -movement_speed_walk;
			}
			else if(in_liquid || in_liquid_stable)
			{
				speedV.Y = -movement_speed_walk;
				swimming_vertical = true;
			}
			else if(is_climbing)
			{
				speedV.Y = -movement_speed_climb;
			}
			else
			{
				// If not free movement but fast is allowed, aux1 is
				// "Turbo button"
				if(fast_move)
					superspeed = true;
			}
		}
	}
	// New minecraft-like descend control
	else
	{
		// Auxiliary button 1 (E)
		if(control.aux1)
		{
			if(!is_climbing)
			{
				// aux1 is "Turbo button"
				if(fast_move)
					superspeed = true;
			}
		}

		if(control.sneak)
		{
			if(free_move)
			{
				// In free movement mode, sneak descends
				if(fast_move && (control.aux1 || g_settings->getBool("always_fly_fast")))
					speedV.Y = -movement_speed_fast;
				else
					speedV.Y = -movement_speed_walk;
			}
			else if(in_liquid || in_liquid_stable)
			{
				if(fast_climb)
					speedV.Y = -movement_speed_fast;
				else
					speedV.Y = -movement_speed_walk;
				swimming_vertical = true;
			}
			else if(is_climbing)
			{
				if(fast_climb)
					speedV.Y = -movement_speed_fast;
				else
					speedV.Y = -movement_speed_climb;
			}
		}
	}

	if(continuous_forward)
		speedH += move_direction;

	if(control.up)
	{
		if(continuous_forward)
			superspeed = true;
		else
			speedH += move_direction;
	}
	if(control.down)
	{
		speedH -= move_direction;
	}
	if(control.left)
	{
		speedH += move_direction.crossProduct(v3f(0,1,0));
	}
	if(control.right)
	{
		speedH += move_direction.crossProduct(v3f(0,-1,0));
	}
	if(control.jump)
	{
		if(free_move)
		{			
			if(g_settings->getBool("aux1_descends") || g_settings->getBool("always_fly_fast"))
			{
				if(fast_move)
					speedV.Y = movement_speed_fast;
				else
					speedV.Y = movement_speed_walk;
			} else {
				if(fast_move && control.aux1)
					speedV.Y = movement_speed_fast;
				else
					speedV.Y = movement_speed_walk;
			}
		}
		else if(m_can_jump)
		{
			/*
				NOTE: The d value in move() affects jump height by
				raising the height at which the jump speed is kept
				at its starting value
			*/
			v3f speedJ = getSpeed();
			if(speedJ.Y >= -0.5 * BS)
			{
				speedJ.Y = movement_speed_jump * physics_override_jump;
				setSpeed(speedJ);
				
				MtEvent *e = new SimpleTriggerEvent("PlayerJump");
				m_gamedef->event()->put(e);
			}
		}
		else if(in_liquid)
		{
			if(fast_climb)
				speedV.Y = movement_speed_fast;
			else
				speedV.Y = movement_speed_walk;
			swimming_vertical = true;
		}
		else if(is_climbing)
		{
			if(fast_climb)
				speedV.Y = movement_speed_fast;
			else
				speedV.Y = movement_speed_climb;
		}
	}

	// The speed of the player (Y is ignored)
	if(superspeed || (is_climbing && fast_climb) || ((in_liquid || in_liquid_stable) && fast_climb))
		speedH = speedH.normalize() * movement_speed_fast;
	else if(control.sneak && !free_move && !in_liquid && !in_liquid_stable)
		speedH = speedH.normalize() * movement_speed_crouch;
	else
		speedH = speedH.normalize() * movement_speed_walk;

	// Acceleration increase
	f32 incH = 0; // Horizontal (X, Z)
	f32 incV = 0; // Vertical (Y)
	if((!touching_ground && !free_move && !is_climbing && !in_liquid) || (!free_move && m_can_jump && control.jump))
	{
		// Jumping and falling
		if(superspeed || (fast_move && control.aux1))
			incH = movement_acceleration_fast * BS * dtime;
		else
			incH = movement_acceleration_air * BS * dtime;
		incV = 0; // No vertical acceleration in air
	}
	else if (superspeed || (is_climbing && fast_climb) || ((in_liquid || in_liquid_stable) && fast_climb))
		incH = incV = movement_acceleration_fast * BS * dtime;
	else
		incH = incV = movement_acceleration_default * BS * dtime;

	// Accelerate to target speed with maximum increment
	accelerateHorizontal(speedH * physics_override_speed, incH * physics_override_speed);
	accelerateVertical(speedV * physics_override_speed, incV * physics_override_speed);
}

v3s16 LocalPlayer::getStandingNodePos()
{
	if(m_sneak_node_exists)
		return m_sneak_node;
	return floatToInt(getPosition(), BS);
}

