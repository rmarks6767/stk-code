//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2009 Marianne Gagnon
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#include "guiengine/engine.hpp"
#include "guiengine/widgets/dynamic_ribbon_widget.hpp"
#include "io/file_manager.hpp"
#include "states_screens/state_manager.hpp"

#include <sstream>

using namespace GUIEngine;
using namespace irr::core;
using namespace irr::gui;

#ifndef round
#  define round(x)  (floor(x+0.5f))
#endif

const char* DynamicRibbonWidget::NO_ITEM_ID = "?";

DynamicRibbonWidget::DynamicRibbonWidget(const bool combo, const bool multi_row) : Widget(WTYPE_DYNAMIC_RIBBON)
{
    m_scroll_offset        = 0;
    m_needed_cols          = 0;
    m_col_amount           = 0;
    m_previous_item_count  = 0;
    m_multi_row            = multi_row;
    m_combo                = combo;
    m_has_label            = false;
    m_left_widget          = NULL;
    m_right_widget         = NULL;
    m_check_inside_me      = true;
    m_supports_multiplayer = true;
    m_scrolling_enabled    = true;
    
    // by default, set all players to have no selection in this ribbon
    for (int n=0; n<MAX_PLAYER_COUNT; n++)
    {
        m_selected_item[n] = -1;
    }
    m_selected_item[0] = 0; // only player 0 has a selection by default
}
// -----------------------------------------------------------------------------
DynamicRibbonWidget::~DynamicRibbonWidget()
{
    if (m_animated_contents)
    {
        GUIEngine::needsUpdate.remove(this);
    }
}

// -----------------------------------------------------------------------------
void DynamicRibbonWidget::add()
{
    //printf("****DynamicRibbonWidget::add()****\n");

    m_has_label = (m_properties[PROP_LABELS_LOCATION] == "bottom");
    
    assert( m_properties[PROP_LABELS_LOCATION] == "bottom" ||
            m_properties[PROP_LABELS_LOCATION] == "each" ||
            m_properties[PROP_LABELS_LOCATION] == "none" ||
            m_properties[PROP_LABELS_LOCATION] == "");
    
    if (m_has_label)
    {
        // FIXME - won't work with multiline labels.
        m_label_height = GUIEngine::getFontHeight();
    }
    else
    {
        m_label_height = 0;
    }
    
    // ----- add dynamic label at bottom
    if (m_has_label)
    {
        // leave room for many lines, just in case (FIXME: remove this hack)
        rect<s32> label_size = rect<s32>(x, y + h - m_label_height, x+w, y+h+m_label_height*5);
        m_label = GUIEngine::getGUIEnv()->addStaticText(L" ", label_size, false, true /* word wrap */, NULL, -1);
        m_label->setTextAlignment( EGUIA_CENTER, EGUIA_UPPERLEFT );
        m_label->setWordWrap(true);
    }
    
    // ---- add arrow buttons on each side
    // FIXME? these arrow buttons are outside of the widget's boundaries
    if (m_left_widget != NULL)
    {
        m_left_widget->elementRemoved();
        m_right_widget->elementRemoved();
        delete m_left_widget;
        delete m_right_widget;
    }
    m_left_widget  = new Widget(WTYPE_NONE);
    m_right_widget = new Widget(WTYPE_NONE);
    
    const int average_y = y + (h-m_label_height)/2;
    m_arrows_w = 30;
    const int button_h = 50;
    
    // right arrow
    rect<s32> right_arrow_location = rect<s32>(x + w - m_arrows_w,
                                               average_y - button_h/2,
                                               x + w,
                                               average_y + button_h/2);
    stringw  rmessage = ">>";
    IGUIButton* right_arrow = GUIEngine::getGUIEnv()->addButton(right_arrow_location, NULL, getNewNoFocusID(), rmessage.c_str(), L"");
    right_arrow->setTabStop(false);
    m_right_widget->m_element = right_arrow;
    m_right_widget->m_event_handler = this;
    m_right_widget->m_focusable = false;
    m_right_widget->m_properties[PROP_ID] = "right";
    m_right_widget->id = right_arrow->getID();
    m_children.push_back( m_right_widget );
    
    // left arrow
    rect<s32> left_arrow_location = rect<s32>(x,
                                              average_y - button_h/2,
                                              x + m_arrows_w,
                                              average_y + button_h/2);
    stringw  lmessage = "<<";
    IGUIButton* left_arrow = GUIEngine::getGUIEnv()->addButton(left_arrow_location, NULL, getNewNoFocusID(), lmessage.c_str(), L"");
    left_arrow->setTabStop(false);
    m_left_widget->m_element = left_arrow;
    m_left_widget->m_event_handler = this;
    m_left_widget->m_focusable = false;
    m_left_widget->m_properties[PROP_ID] = "left";
    m_left_widget->id = left_arrow->getID();
    m_children.push_back( m_left_widget );
    
    // ---- Determine number of rows and columns
    
    // Find children size (and ratio)
    m_child_width  = atoi(m_properties[PROP_CHILD_WIDTH].c_str());
    m_child_height = atoi(m_properties[PROP_CHILD_HEIGHT].c_str());
    
    if (m_child_width <= 0 || m_child_height <= 0)
    {
        std::cerr << "/!\\ Warning /!\\ : ribbon grid widgets require 'child_width' and 'child_height' arguments" << std::endl;
        m_child_width = 256;
        m_child_height = 256;
    }
    
    // determine row amount
    m_row_amount = (int)round((h-m_label_height) / (float)m_child_height);
    
    if (m_properties[PROP_MAX_ROWS].size() > 0)
    {
        const int max_rows = atoi(m_properties[PROP_MAX_ROWS].c_str());
        if (max_rows < 1)
        {
            std::cout << "/!\\ WARNING : the 'max_rows' property must be an integer greater than zero."
                      << " Ingoring current value '" << m_properties[PROP_MAX_ROWS] << "'\n"; 
        }
        else
        {
            if (m_row_amount > max_rows) m_row_amount = max_rows;
        }
    }
    // get and build a list of IDs (by now we may not yet know everything about items,
    // but we need to get IDs *now* in order for tabbing to work.
    m_ids.resize(m_row_amount);
    for (int i=0; i<m_row_amount; i++)
    {
        m_ids[i] = getNewID();
        //std::cout << "ribbon : getNewID returns " <<  m_ids[i] << std::endl;
    }
    
    buildInternalStructure();
}
// -----------------------------------------------------------------------------
void DynamicRibbonWidget::buildInternalStructure()
{
    //printf("****DynamicRibbonWidget::buildInternalStructure()****\n");
    
    // ---- Clean-up what was previously there
    for (int i=0; i<m_children.size(); i++)
    {
        IGUIElement* elem = m_children[i].m_element;
        if (elem != NULL && m_children[i].m_type == WTYPE_RIBBON)
        {
            elem->remove();
            m_children.erase(i);
            i--;
            if (i<0) i = 0;
        }
    }
    m_rows.clearWithoutDeleting(); // rows already deleted above, don't double-delete
    
    // ---- determine column amount
    const float row_height = (float)(h - m_label_height)/(float)m_row_amount;
    float ratio_zoom = (float)row_height / (float)(m_child_height - m_label_height);
    m_col_amount = (int)round( w / ( m_child_width*ratio_zoom ) );
    
    
    // ajust column amount to not add more item slots than we actually need
    const int item_count = m_items.size();
    //std::cout << "item_count=" << item_count << ", row_amount*m_col_amount=" << m_row_amount*m_col_amount << std::endl;
    if (m_row_amount*m_col_amount > item_count)
    {
        m_col_amount = (int)ceil((float)item_count/(float)m_row_amount);
        //std::cout << "Adjusting m_col_amount to be " << m_col_amount << std::endl;
    }
        
    // Hide arrows when everything is visible
    if (item_count <= m_row_amount*m_col_amount)
    {
        m_scrolling_enabled = false;
        m_left_widget->m_element->setVisible(false);
        m_right_widget->m_element->setVisible(false);
    }
    else
    {
        m_scrolling_enabled = true;
        m_left_widget->m_element->setVisible(true);
        m_right_widget->m_element->setVisible(true);
    }
    
    // ---- add rows
    int added_item_count = 0;
    for (int n=0; n<m_row_amount; n++)
    {
        RibbonWidget* ribbon;
        if (m_combo)
        {
            ribbon = new RibbonWidget(RIBBON_COMBO);
        }
        else
        {
            ribbon = new RibbonWidget(RIBBON_TOOLBAR);
        }
        ribbon->setListener(this);
        ribbon->m_reserved_id = m_ids[n];
                
        ribbon->x = x + m_arrows_w;
        ribbon->y = y + (int)(n*row_height);
        ribbon->w = w - m_arrows_w*2;
        ribbon->h = (int)(row_height);
        ribbon->m_type = WTYPE_RIBBON;

        std::stringstream name;
        name << this->m_properties[PROP_ID] << "_row" << n;
        ribbon->m_properties[PROP_ID] = name.str();
        ribbon->m_event_handler = this;
        
        // add columns
        for (int i=0; i<m_col_amount; i++)
        {
            // stretch the *texture* within the widget (and the widget has the right aspect ratio)
            // (Yeah, that's complicated, but screenshots are saved compressed horizontally so it's hard to be clean)
            IconButtonWidget* icon = new IconButtonWidget(IconButtonWidget::SCALE_MODE_STRETCH, false, true);
            icon->m_properties[PROP_ICON]="textures/transparence.png";
            
            // set size to get proper ratio (as most textures are saved scaled down to 256x256)
            icon->m_properties[PROP_WIDTH] = m_properties[PROP_CHILD_WIDTH];
            icon->m_properties[PROP_HEIGHT] = m_properties[PROP_CHILD_HEIGHT];
            icon->w = atoi(icon->m_properties[PROP_WIDTH].c_str());
            icon->h = atoi(icon->m_properties[PROP_HEIGHT].c_str());

            // If we want each icon to have its own label, we must make it non-empty, otherwise
            // it will assume there is no label and none will be created (FIXME: that's ugly)
            if (m_properties[PROP_LABELS_LOCATION] == "each") icon->m_text = " ";
            
            // std::cout << "ribbon text = " << m_properties[PROP_TEXT].c_str() << std::endl;
            
            ribbon->m_children.push_back( icon );
            added_item_count++;
            
            // stop adding columns when we have enough items
            if (added_item_count == item_count)
            {
                assert(!m_scrolling_enabled); // we can see all items, so scrolling must be off
                break; 
            }
            else if (added_item_count > item_count)
            {
                assert(false);
                break;
            }
        }
        m_children.push_back( ribbon );
        m_rows.push_back( ribbon );
        ribbon->add();
        
        // stop filling rows when we have enough items
        if (added_item_count == item_count)
        {
            assert(!m_scrolling_enabled); // we can see all items, so scrolling must be off
            break; 
        }
    }
    
#ifdef DEBUG
    if (!m_scrolling_enabled)
    {
        // debug checks
        int childrenCount = 0;
        for (int n=0; n<m_rows.size(); n++)
        {
            childrenCount += m_rows[n].m_children.size();
        }
        assert(childrenCount == (int)m_items.size());
    }
#endif
 }
// -----------------------------------------------------------------------------
void DynamicRibbonWidget::addItem( const irr::core::stringw& user_name, const std::string& code_name,
                                   const std::string& image_file, const unsigned int badges,
                                   IconButtonWidget::IconPathType image_path_type)
{
    ItemDescription desc;
    desc.m_user_name = user_name;
    desc.m_code_name = code_name;
    desc.m_sshot_file = image_file;
    desc.m_badges = badges;
    desc.m_animated = false;
    desc.m_image_path_type = image_path_type;
    
    m_items.push_back(desc);
}

// -----------------------------------------------------------------------------

void DynamicRibbonWidget::addAnimatedItem( const irr::core::stringw& user_name, const std::string& code_name,
                                           const std::vector<std::string>& image_files, const float time_per_frame,
                                           const unsigned int badges, IconButtonWidget::IconPathType image_path_type )
{
    ItemDescription desc;
    desc.m_user_name = user_name;
    desc.m_code_name = code_name;
    desc.m_all_images = image_files;
    desc.m_badges = badges;
    desc.m_animated = true;
    desc.m_curr_time = 0.0f;
    desc.m_time_per_frame = time_per_frame;
    desc.m_image_path_type = image_path_type;
    
    m_items.push_back(desc);
    
    if (!m_animated_contents)
    {
        m_animated_contents = true;
        
        /*
         FIXME: remove this unclean thing, I think irrlicht provides this feature:
         virtual void IGUIElement::OnPostRender (u32 timeMs)
         \brief animate the element and its children. 
         FIXME 2: I think it will remain in the needsUpdate even when leaving the screen?
         */
        GUIEngine::needsUpdate.push_back(this);
    }
}

// -----------------------------------------------------------------------------
void DynamicRibbonWidget::clearItems()
{
    m_items.clear();
}
// -----------------------------------------------------------------------------
void DynamicRibbonWidget::elementRemoved()
{
    Widget::elementRemoved();
    m_previous_item_count = 0;
    m_rows.clearWithoutDeleting();
    m_left_widget = NULL;
    m_right_widget = NULL;
    
    m_hover_listeners.clearAndDeleteAll();
}


#if 0
#pragma mark -
#pragma mark Getters
#endif

const std::string& DynamicRibbonWidget::getSelectionIDString(const int playerID)
{
    RibbonWidget* row = (RibbonWidget*)(m_rows.size() == 1 ? m_rows.get(0) : getSelectedRibbon(playerID));
    
    if (row != NULL) return row->getSelectionIDString(playerID);
    
    static const std::string nothing = "";
    return nothing;
}
// -----------------------------------------------------------------------------
const irr::core::stringw& DynamicRibbonWidget::getSelectionText(const int playerID)
{
    RibbonWidget* row = (RibbonWidget*)(m_rows.size() == 1 ? m_rows.get(0) : getSelectedRibbon(playerID));
    
    if (row != NULL) return row->getSelectionText(playerID);
    
    static const irr::core::stringw nothing = "";
    return nothing;
}
// -----------------------------------------------------------------------------
RibbonWidget* DynamicRibbonWidget::getRowContaining(Widget* w) const
{
    const int row_amount = m_rows.size();
    for(int n=0; n<row_amount; n++)
    {
        const RibbonWidget* row = &m_rows[n];
        if(row != NULL)
        {
            if(m_children.contains( w ) ) return (RibbonWidget*)row;
        }
    }
    
    return NULL;
}
// -----------------------------------------------------------------------------
RibbonWidget* DynamicRibbonWidget::getSelectedRibbon(const int playerID) const
{    

    const int row_amount = m_rows.size();
    for(int n=0; n<row_amount; n++)
    {
        const RibbonWidget* row = &m_rows[n];
        if (GUIEngine::isFocusedForPlayer(row, playerID))
        {
            return (RibbonWidget*)row;
        }
    }
        

    return NULL;
}

#if 0
#pragma mark -
#pragma mark Event Handling
#endif

void DynamicRibbonWidget::registerHoverListener(DynamicRibbonHoverListener* listener)
{
    m_hover_listeners.push_back(listener);
}
// -----------------------------------------------------------------------------
EventPropagation DynamicRibbonWidget::rightPressed(const int playerID)
{    
    if (m_deactivated) return EVENT_LET;
    
    RibbonWidget* w = getSelectedRibbon(playerID);
    if (w != NULL)
    {
        updateLabel();
        
        propagateSelection();
        
        const int listenerAmount = m_hover_listeners.size();
        for (int n=0; n<listenerAmount; n++)
        {
            m_hover_listeners[n].onSelectionChanged(this, getSelectedRibbon(playerID)->getSelectionIDString(playerID),
                                                    getSelectedRibbon(playerID)->getSelectionText(playerID), playerID);
        }
    }
    //std::cout << "rightpressed (dynamic ribbon) " << m_properties[PROP_ID] << "\n";
    
    assert(m_rows.size() >= 1);
    if (m_rows[0].m_ribbon_type == RIBBON_TOOLBAR) return EVENT_BLOCK;
    
    //std::cout << "     rightpressed returning EVENT_LET\n";

    return EVENT_LET;
}
// -----------------------------------------------------------------------------
EventPropagation DynamicRibbonWidget::leftPressed(const int playerID)
{    
    if (m_deactivated) return EVENT_LET;
    
    RibbonWidget* w = getSelectedRibbon(playerID);
    if (w != NULL)
    {
        updateLabel();
        propagateSelection();
        
        const int listenerAmount = m_hover_listeners.size();
        for (int n=0; n<listenerAmount; n++)
        {
            m_hover_listeners[n].onSelectionChanged(this, w->getSelectionIDString(playerID),
                                                    w->getSelectionText(playerID), playerID);
        }
    }
    
    assert(m_rows.size() >= 1);
    if (m_rows[0].m_ribbon_type == RIBBON_TOOLBAR) return EVENT_BLOCK;
    
    return EVENT_LET;
}
// -----------------------------------------------------------------------------
EventPropagation DynamicRibbonWidget::transmitEvent(Widget* w, std::string& originator, const int playerID)
{
    if (m_deactivated) return EVENT_LET;

    if (originator=="left")
    {
        scroll(-1);
        return EVENT_BLOCK;
    }
    if (originator=="right")
    {
        scroll(1);
        return EVENT_BLOCK;
    }
    
    // find selection in current ribbon
    if (m_combo)
    {
        RibbonWidget* selected_ribbon = (RibbonWidget*)getSelectedRibbon(playerID);
        if (selected_ribbon != NULL)
        {
            m_selected_item[playerID] = selected_ribbon->m_selection[playerID] + m_scroll_offset;
            if (m_selected_item[playerID] >= (int)m_items.size()) m_selected_item[playerID] -= m_items.size();
        }
    }
    
    return EVENT_LET;
}
// -----------------------------------------------------------------------------
EventPropagation DynamicRibbonWidget::mouseHovered(Widget* child, const int playerID)
{
    if (m_deactivated) return EVENT_LET;
    //std::cout << "DynamicRibbonWidget::mouseHovered " << playerID << std::endl;

    updateLabel();
    propagateSelection();
    
    if (getSelectedRibbon(playerID) != NULL)
    {
        const int listenerAmount = m_hover_listeners.size();
        for (int n=0; n<listenerAmount; n++)
        {
            m_hover_listeners[n].onSelectionChanged(this, getSelectedRibbon(playerID)->getSelectionIDString(playerID),
                                                    getSelectedRibbon(playerID)->getSelectionText(playerID), playerID);
        }
    }
    
    return EVENT_BLOCK;
}
// -----------------------------------------------------------------------------
EventPropagation DynamicRibbonWidget::focused(const int playerID)
{
    Widget::focused(playerID);
    updateLabel();
    
    const int listenerAmount = m_hover_listeners.size();
    for(int n=0; n<listenerAmount; n++)
    {
        m_hover_listeners[n].onSelectionChanged(this, getSelectedRibbon(playerID)->getSelectionIDString(playerID),
                                                getSelectedRibbon(playerID)->getSelectionText(playerID), playerID);
    }
    
    return EVENT_LET;
}

// -----------------------------------------------------------------------------

void DynamicRibbonWidget::onRibbonWidgetScroll(const int delta_x)
{
    scroll(delta_x);
}

// -----------------------------------------------------------------------------

void DynamicRibbonWidget::onRibbonWidgetFocus(RibbonWidget* emitter, const int playerID)
{
    if (m_deactivated) return;
    
    if (emitter->m_selection[playerID] >= emitter->m_children.size())
    {
        emitter->m_selection[playerID] = emitter->m_children.size()-1;
    }
    
    updateLabel(emitter);
    
    const int listenerAmount = m_hover_listeners.size();
    for (int n=0; n<listenerAmount; n++)
    {
        m_hover_listeners[n].onSelectionChanged(this, emitter->getSelectionIDString(playerID),
                                                emitter->getSelectionText(playerID), playerID);
    }    
}

#if 0
#pragma mark -
#pragma mark Setters / Actions
#endif

void DynamicRibbonWidget::scroll(const int x_delta)
{
    // Refuse to scroll when everything is visible
    if ((int)m_items.size() <= m_row_amount*m_col_amount) return;
    
    m_scroll_offset += x_delta;
    
    const int max_scroll = std::max(m_col_amount, m_needed_cols) - 1;
    
    if (m_scroll_offset < 0) m_scroll_offset = max_scroll;
    else if (m_scroll_offset > max_scroll) m_scroll_offset = 0;
    
    updateItemDisplay();
    
    // update selection markers in child ribbon
    if (m_combo)
    {
        for (int n=0; n<MAX_PLAYER_COUNT; n++)
        {
            RibbonWidget* ribbon = m_rows.get(0); // there is a single row when we can select items
            int id = m_selected_item[n] - m_scroll_offset;
            if (id < 0) id += m_items.size();
            ribbon->setSelection(id, n);
        }
    }    
}
// -----------------------------------------------------------------------------
/** DynamicRibbonWidget is made of several ribbons; each of them thus has
 its own selection independently of each other. To keep a grid feeling
 (i.e. you remain in the same column when pressing up/down), this method is
 used to ensure that all children ribbons always select the same column */
void DynamicRibbonWidget::propagateSelection()
{    
    for (int p=0; p<MAX_PLAYER_COUNT; p++)
    {
        // find selection in current ribbon
        RibbonWidget* selected_ribbon = (RibbonWidget*)getSelectedRibbon(p);
        if (selected_ribbon == NULL) continue;
        
        const int relative_selection = selected_ribbon->m_selection[p];
        float where = 0.0f;
        
        if (selected_ribbon->m_children.size() > 1)
        {
            where = (float)relative_selection / (float)(selected_ribbon->m_children.size() - 1);
        }
        else
        {
            where = 0.0f;
        }
        assert(where >= 0.0f);
        assert(where <= 1.0f);

        if (m_combo)
        {
            m_selected_item[p] = relative_selection + m_scroll_offset;
            if (m_selected_item[p] >= (int)m_items.size()) m_selected_item[p] -= m_items.size();
        }
        
        // set same selection in all ribbons
        const int row_amount = m_rows.size();
        for (int n=0; n<row_amount; n++)
        {
            RibbonWidget* ribbon = m_rows.get(n);
            if (ribbon != selected_ribbon)
            {
                ribbon->m_selection[p] = (int)round(where*(ribbon->m_children.size()-1));
                ribbon->updateSelection();
            }
        }
        
    }
}
// -----------------------------------------------------------------------------
void DynamicRibbonWidget::updateLabel(RibbonWidget* from_this_ribbon)
{
    if (!m_has_label) return;
    
    // only the master player can update the label
    const int playerID = PLAYER_ID_GAME_MASTER;
    
    RibbonWidget* row = from_this_ribbon ? from_this_ribbon : (RibbonWidget*)getSelectedRibbon(playerID);
    if (row == NULL) return;
    
    std::string selection_id = row->getSelectionIDString(playerID);
    
    const int amount = m_items.size();
    for (int n=0; n<amount; n++)
    {
        if (m_items[n].m_code_name == selection_id)
        {
            m_label->setText( stringw(m_items[n].m_user_name.c_str()).c_str() );
            return;
        }
    }
    
    if (selection_id == NO_ITEM_ID) m_label->setText( L"" );
    else                            m_label->setText( L"Unknown Item" );
}

// -----------------------------------------------------------------------------

/** Set to 1 if you wish information about item placement within the ribbon to be printed */
#define CHATTY_ABOUT_ITEM_PLACEMENT 0

void DynamicRibbonWidget::updateItemDisplay()
{
    // ---- Check if we need to update the number of icons in the ribbon
    if ((int)m_items.size() != m_previous_item_count)
    {
        buildInternalStructure();
        m_previous_item_count = m_items.size();
    }
    
    // ---- some variables
    int icon_id = 0;
    
    const int row_amount = m_rows.size();
    const int item_amount = m_items.size();
    
    //FIXME: isn't this set by 'buildInternalStructure' already?
    m_needed_cols = (int)ceil( (float)item_amount / (float)row_amount );
    
    //const int max_scroll = std::max(m_col_amount, m_needed_cols) - 1;
    
    // the number of items that fit perfectly the number of rows we have
    // (this value will be useful to compute scrolling)
    int fitting_item_amount = (m_scrolling_enabled ? m_needed_cols * row_amount : m_items.size());
    
    // ---- to determine which items go in which cell of the dynamic ribbon now,
    //      we create a temporary 2D table and fill them with the ID of the item
    //      they need to display.
    //int item_placement[row_amount][m_needed_cols];
    std::vector<std::vector<int> > item_placement;
    item_placement.resize(row_amount);
    for(int i=0; i<row_amount; i++)
        item_placement[i].resize(m_needed_cols);
    
    int counter = 0;
    
#if CHATTY_ABOUT_ITEM_PLACEMENT
    std::cout << m_items.size() << " items to be placed:\n{\n";
#endif
    
    for (int c=0; c<m_needed_cols; c++)
    {
        for (int r=0; r<row_amount; r++)
        {
            
#if CHATTY_ABOUT_ITEM_PLACEMENT
            std::cout << "    ";
#endif
            
            const int items_in_row = m_rows[r].m_children.size();
            if (c >= items_in_row)
            {
                item_placement[r][c] = -1;
                
#if CHATTY_ABOUT_ITEM_PLACEMENT
                std::cout << item_placement[r][c] << "  ";
#endif
                continue;
            }
            
            int newVal = counter + m_scroll_offset*row_amount;
            while (newVal >= fitting_item_amount) newVal -= fitting_item_amount;
            item_placement[r][c] = newVal;
            
#if CHATTY_ABOUT_ITEM_PLACEMENT
            std::cout << newVal << "  ";
#endif
            
            counter++;
        }
        
#if CHATTY_ABOUT_ITEM_PLACEMENT
        std::cout << "\n";
#endif
        
    }
    
#if CHATTY_ABOUT_ITEM_PLACEMENT
    std::cout << "}\n";
#endif
    
    // ---- iterate through the rows, and set the items of each row to match those of the table
    for (int n=0; n<row_amount; n++)
    {
        RibbonWidget& row = m_rows[n];
        
        //std::cout << "Row " << n << "\n{\n";
        
        const int items_in_row = row.m_children.size();
        for (int i=0; i<items_in_row; i++)
        {
            IconButtonWidget* icon = dynamic_cast<IconButtonWidget*>(&row.m_children[i]);
            assert(icon != NULL);
            
            icon_id = item_placement[n][i];
            
            if (icon_id < item_amount && icon_id != -1)
            {
                std::string item_icon = (m_items[icon_id].m_animated ?
                                         m_items[icon_id].m_all_images[0] :
                                         m_items[icon_id].m_sshot_file);
                icon->setImage( item_icon.c_str(), m_items[icon_id].m_image_path_type );
                
                icon->m_properties[PROP_ID]   = m_items[icon_id].m_code_name;
                icon->setLabel(m_items[icon_id].m_user_name);
                icon->m_text                  = m_items[icon_id].m_user_name;
                icon->m_badges                = m_items[icon_id].m_badges;
                
                //std::cout << "    item " << i << " is " << m_items[icon_id].m_code_name << "\n";
                
                //std::wcout << L"Setting widget text '" << icon->m_text.c_str() << L"'\n";
                
                // if the ribbon has no "ribbon-wide" label, call will do nothing
                row.setLabel(i, m_items[icon_id].m_user_name);
            }
            else
            {
                icon->setImage( "textures/transparence.png", IconButtonWidget::ICON_PATH_TYPE_RELATIVE );
                icon->m_properties[PROP_ID] = NO_ITEM_ID;
                //std::cout << "    item " << i << " is a FILLER\n";
            }
        } // next column
    } // next row
}

// -----------------------------------------------------------------------------

void DynamicRibbonWidget::update(float dt)
{
    const int row_amount = m_rows.size();
    for (int n=0; n<row_amount; n++)
    {
        RibbonWidget& row = m_rows[n];
        
        const int items_in_row = row.m_children.size();
        for (int i=0; i<items_in_row; i++)
        {
            int col_scroll = i + m_scroll_offset;
            int item_id = (col_scroll)*row_amount + n;
            if (item_id >= (int)m_items.size()) item_id -= m_items.size();
            
            assert(item_id >= 0);
            assert(item_id < (int)m_items.size());
            
            //m_items[icon_id].
            
            if (m_items[item_id].m_animated)
            {
                const int frameBefore = (int)(m_items[item_id].m_curr_time / m_items[item_id].m_time_per_frame);

                m_items[item_id].m_curr_time += dt;
                int frameAfter = (int)(m_items[item_id].m_curr_time / m_items[item_id].m_time_per_frame);
                if (frameAfter == frameBefore) continue; // no frame change yet
                
                if (frameAfter >= (int)m_items[item_id].m_all_images.size())
                {
                    m_items[item_id].m_curr_time = 0;
                    frameAfter = 0;
                }
                
                IconButtonWidget* icon = dynamic_cast<IconButtonWidget*>(&row.m_children[i]);
                icon->setImage( m_items[item_id].m_all_images[frameAfter].c_str() );
            }

        }
    }
}

// -----------------------------------------------------------------------------
bool DynamicRibbonWidget::setSelection(int item_id, const int playerID, const bool focusIt)
{
    //printf("****DynamicRibbonWidget::setSelection()****\n");

    m_selected_item[playerID] = item_id;
    

    const std::string& name = m_items[item_id].m_code_name;
    
    int row = -1;
    int id;
    
    for (int r=0; r<m_row_amount; r++)
    {
        //printf("Looking for %s in row %i\n", name.c_str(), r);
        id = m_rows[r].findItemNamed(name.c_str());
        if (id > -1)
        {
            row = r;
            break;
        }
    }
    
    if (row == -1)
    {
        std::cerr << "DynamicRibbonWidget::setSelection cannot find item " << item_id << " (" << name.c_str() << ")\n";
        return false;
    }
    
    //std::cout << "Player " << playerID << " has item " << item_id << " (" << name.c_str() << ") in row " << row << std::endl;
    m_rows[row].setSelection(id, playerID);
    if (focusIt) m_rows[row].setFocusForPlayer(playerID);
    
    propagateSelection();
    return true;
}
// -----------------------------------------------------------------------------
bool DynamicRibbonWidget::setSelection(const std::string item_codename, const int playerID, const bool focusIt)
{
    const int item_count = m_items.size();
    for (int n=0; n<item_count; n++)
    {
        if (m_items[n].m_code_name == item_codename)
        {
            return setSelection(n, playerID, focusIt);
        }
    }
    return false;
}

// -----------------------------------------------------------------------------


