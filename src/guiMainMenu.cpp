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

#include "guiMainMenu.h"
#include "guiKeyChangeMenu.h"
#include "guiCreateWorld.h"
#include "guiConfigureWorld.h"
#include "guiMessageMenu.h"
#include "guiConfirmMenu.h"
#include "debug.h"
#include "serialization.h"
#include <string>
#include <IGUICheckBox.h>
#include <IGUIEditBox.h>
#include <IGUIButton.h>
#include <IGUIStaticText.h>
#include <IGUIFont.h>
#include <IGUIListBox.h>
#include <IGUITabControl.h>
#include <IGUIImage.h>
// For IGameCallback
#include "guiPauseMenu.h"
#include "gettext.h"
#include "tile.h" // getTexturePath
#include "filesys.h"
#include "util/string.h"
#include "subgame.h"

#define ARRAYLEN(x) (sizeof(x) / sizeof((x)[0]))
#define LSTRING(x) LSTRING_(x)
#define LSTRING_(x) L##x

enum
{
	GUI_ID_WORLD_LISTBOX,
};

GUIMainMenu::GUIMainMenu(gui::IGUIEnvironment* env,
		gui::IGUIElement* parent, s32 id,
		IMenuManager *menumgr,
		MainMenuData *data,
		IGameCallback *gamecallback
):
	GUIModalMenu(env, parent, id, menumgr),
	m_data(data),
	m_accepted(false),
	m_gamecallback(gamecallback),
	m_is_regenerating(false)
{
	assert(m_data);
	this->env = env;
	this->parent = parent;
	this->id = id;
	this->menumgr = menumgr;
}

GUIMainMenu::~GUIMainMenu()
{
	removeChildren();
}

void GUIMainMenu::removeChildren()
{
	const core::list<gui::IGUIElement*> &children = getChildren();
	core::list<gui::IGUIElement*> children_copy;
	for(core::list<gui::IGUIElement*>::ConstIterator
			i = children.begin(); i != children.end(); i++)
	{
		children_copy.push_back(*i);
	}
	for(core::list<gui::IGUIElement*>::Iterator
			i = children_copy.begin();
			i != children_copy.end(); i++)
	{
		(*i)->remove();
	}
}

void GUIMainMenu::regenerateGui(v2u32 screensize)
{
	m_is_regenerating = true;
	/*
		Read stuff from elements into m_data
	*/
	readInput(m_data);

	/*
		Remove stuff
	*/
	removeChildren();
	
	/*
		Calculate new sizes and positions
	*/
	
	v2s32 size(screensize.X, screensize.Y);

	core::rect<s32> rect(
			screensize.X/2 - size.X/2,
			screensize.Y/2 - size.Y/2,
			screensize.X/2 + size.X/2,
			screensize.Y/2 + size.Y/2
	);

	DesiredRect = rect;
	recalculateAbsolutePosition(false);

	//v2s32 size = rect.getSize();

	/*
		Add stuff
	*/

	changeCtype("");
	{
		core::rect<s32> rect(0, 0, 256,256);
		gui::IGUIListBox *e = Environment->addListBox(rect, this,
				GUI_ID_WORLD_LISTBOX);
		e->setDrawBackground(true);
		m_world_indices.clear();
		for(size_t wi = 0; wi < m_data->worlds.size(); wi++){
			const WorldSpec &spec = m_data->worlds[wi];
			e->addItem(narrow_to_wide(spec.name+" ["+spec.gameid+"]").c_str());
			m_world_indices.push_back(wi);
			if(m_data->selected_world == (int)wi)
				e->setSelected(m_world_indices.size()-1);
		}
		Environment->setFocus(e);
	}

	m_is_regenerating = false;
}

void GUIMainMenu::drawMenu()
{
	gui::IGUISkin* skin = Environment->getSkin();
	if (!skin)
		return;
	video::IVideoDriver* driver = Environment->getVideoDriver();

	/* Draw UI elements */

	gui::IGUIElement::draw();
}

void GUIMainMenu::readInput(MainMenuData *dst)
{
	dst->simple_singleplayer_mode = true;
	{
		gui::IGUIElement *e = getElementFromId(GUI_ID_WORLD_LISTBOX);
		if(e != NULL && e->getType() == gui::EGUIET_LIST_BOX){
			int list_i = ((gui::IGUIListBox*)e)->getSelected();
			if(list_i == -1)
				dst->selected_world = -1;
			else
				dst->selected_world = m_world_indices[list_i];
		}
	}
}

void GUIMainMenu::acceptInput()
{
	readInput(m_data);
	m_accepted = true;
}

bool GUIMainMenu::OnEvent(const SEvent& event)
{
	if(event.EventType==EET_KEY_INPUT_EVENT)
	{
		if(event.KeyInput.Key==KEY_ESCAPE && event.KeyInput.PressedDown)
		{
			m_gamecallback->exitToOS();
			quitMenu();
			return true;
		}
		if(event.KeyInput.Key==KEY_RETURN && event.KeyInput.PressedDown)
		{
			acceptInput();
			quitMenu();
			return true;
		}
	}
	if(event.EventType==EET_GUI_EVENT)
	{
		if(event.GUIEvent.EventType==gui::EGET_ELEMENT_FOCUS_LOST
				&& isVisible())
		{
			if(!canTakeFocus(event.GUIEvent.Element))
			{
				dstream<<"GUIMainMenu: Not allowing focus change."
						<<std::endl;
				// Returning true disables focus change
				return true;
			}
		}
		if(event.GUIEvent.EventType==gui::EGET_LISTBOX_CHANGED)
		{
			readInput(m_data);
		}
		if(event.GUIEvent.EventType==gui::EGET_LISTBOX_SELECTED_AGAIN)
		{
			switch(event.GUIEvent.Caller->getID())
			{
			case GUI_ID_WORLD_LISTBOX:
				acceptInput();
				if(getTab() != TAB_SINGLEPLAYER)
					m_data->address = L""; // Force local game
				quitMenu();
				return true;
			}
		}
	}

	return Parent ? Parent->OnEvent(event) : false;
}
	
int GUIMainMenu::getTab()
{
	return TAB_SINGLEPLAYER; // Default
}

void GUIMainMenu::displayMessageMenu(std::wstring msg)
{
	(new GUIMessageMenu(env, parent, -1, menumgr, msg))->drop();
}
