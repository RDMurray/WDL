/* Cockos SWELL (Simple/Small Win32 Emulation Layer for Linux/OSX)
   Copyright (C) 2006 and later, Cockos, Inc.

    This software is provided 'as-is', without any express or implied
    warranty.  In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
       claim that you wrote the original software.
    2. Altered source versions must be plainly marked as such, and must not be
       misrepresented as being the original software.
    3. This notice may not be removed or altered from any source distribution.
*/

#ifndef SWELL_PROVIDED_BY_APP

#include "swell.h"
#include "swell-internal.h"
#include "../wdlutf8.h"

#if defined(SWELL_TARGET_GDK) && defined(SWELL_ATSPI)

#include <gio/gio.h>

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

#include <string>
#include <vector>

static const char *SWELL_ATSPI_REGISTRY_NAME = "org.a11y.atspi.Registry";
static const char *SWELL_ATSPI_ROOT_PATH = "/org/a11y/atspi/accessible/root";
static const char *SWELL_ATSPI_NULL_PATH = "/org/a11y/atspi/null";
static const char *SWELL_ATSPI_ACCESSIBLE_PREFIX = "/org/a11y/atspi/accessible";
static const char *SWELL_ATSPI_ACCESSIBLE_IFACE = "org.a11y.atspi.Accessible";
static const char *SWELL_ATSPI_APPLICATION_IFACE = "org.a11y.atspi.Application";
static const char *SWELL_ATSPI_COMPONENT_IFACE = "org.a11y.atspi.Component";
static const char *SWELL_ATSPI_ACTION_IFACE = "org.a11y.atspi.Action";
static const char *SWELL_ATSPI_VALUE_IFACE = "org.a11y.atspi.Value";
static const char *SWELL_ATSPI_TEXT_IFACE = "org.a11y.atspi.Text";
static const char *SWELL_ATSPI_EDITABLE_TEXT_IFACE = "org.a11y.atspi.EditableText";

enum
{
  SWELL_ATSPI_STATE_ACTIVE = 1,
  SWELL_ATSPI_STATE_CHECKED = 4,
  SWELL_ATSPI_STATE_DEFUNCT = 6,
  SWELL_ATSPI_STATE_EDITABLE = 7,
  SWELL_ATSPI_STATE_ENABLED = 8,
  SWELL_ATSPI_STATE_FOCUSABLE = 11,
  SWELL_ATSPI_STATE_FOCUSED = 12,
  SWELL_ATSPI_STATE_SELECTED = 23,
  SWELL_ATSPI_STATE_SENSITIVE = 24,
  SWELL_ATSPI_STATE_SHOWING = 25,
  SWELL_ATSPI_STATE_SINGLE_LINE = 26,
  SWELL_ATSPI_STATE_VISIBLE = 29
};

enum
{
  SWELL_ATSPI_ROLE_INVALID = 0,
  SWELL_ATSPI_ROLE_CHECK_BOX = 7,
  SWELL_ATSPI_ROLE_COMBO_BOX = 11,
  SWELL_ATSPI_ROLE_DIALOG = 16,
  SWELL_ATSPI_ROLE_FILLER = 20,
  SWELL_ATSPI_ROLE_FRAME = 23,
  SWELL_ATSPI_ROLE_LABEL = 29,
  SWELL_ATSPI_ROLE_LIST = 31,
  SWELL_ATSPI_ROLE_PAGE_TAB_LIST = 38,
  SWELL_ATSPI_ROLE_PANEL = 39,
  SWELL_ATSPI_ROLE_PROGRESS_BAR = 42,
  SWELL_ATSPI_ROLE_PUSH_BUTTON = 43,
  SWELL_ATSPI_ROLE_RADIO_BUTTON = 44,
  SWELL_ATSPI_ROLE_SLIDER = 51,
  SWELL_ATSPI_ROLE_TABLE = 55,
  SWELL_ATSPI_ROLE_TEXT = 61,
  SWELL_ATSPI_ROLE_TREE = 65,
  SWELL_ATSPI_ROLE_WINDOW = 69,
  SWELL_ATSPI_ROLE_APPLICATION = 75,
  SWELL_ATSPI_ROLE_ENTRY = 79,
  SWELL_ATSPI_ROLE_LIST_BOX = 98
};

struct SWELL_AtspiInterfaceSet
{
  bool accessible;
  bool application;
  bool component;
  bool action;
  bool value;
  bool text;
  bool editable_text;
};

static GDBusConnection *g_atspi_bus;
static GDBusNodeInfo *g_atspi_node_info;
static guint g_atspi_subtree_id;
static bool g_atspi_initialized;
static bool g_atspi_embedded;
static bool g_atspi_debug;
static HWND g_atspi_focused_hwnd;

struct SWELL_AtspiObjectRecord
{
  std::string path;
  HWND hwnd;
  bool defunct;
};

static std::vector<SWELL_AtspiObjectRecord> g_atspi_objects;

static const char g_atspi_xml[] =
"<node>"
"  <interface name='org.a11y.atspi.Accessible'>"
"    <property name='Name' type='s' access='read'/>"
"    <property name='Description' type='s' access='read'/>"
"    <property name='Parent' type='(so)' access='read'/>"
"    <property name='ChildCount' type='i' access='read'/>"
"    <method name='GetChildAtIndex'><arg name='index' type='i' direction='in'/><arg name='child' type='(so)' direction='out'/></method>"
"    <method name='GetChildren'><arg name='children' type='a(so)' direction='out'/></method>"
"    <method name='GetIndexInParent'><arg name='index' type='i' direction='out'/></method>"
"    <method name='GetRole'><arg name='role' type='u' direction='out'/></method>"
"    <method name='GetRoleName'><arg name='name' type='s' direction='out'/></method>"
"    <method name='GetLocalizedRoleName'><arg name='name' type='s' direction='out'/></method>"
"    <method name='GetState'><arg name='states' type='au' direction='out'/></method>"
"    <method name='GetRelationSet'><arg name='relations' type='a(ua(so))' direction='out'/></method>"
"    <method name='GetAttributes'><arg name='attributes' type='a{ss}' direction='out'/></method>"
"    <method name='GetApplication'><arg name='application' type='(so)' direction='out'/></method>"
"    <method name='GetInterfaces'><arg name='interfaces' type='as' direction='out'/></method>"
"    <method name='GetLocale'><arg name='locale' type='s' direction='out'/></method>"
"  </interface>"
"  <interface name='org.a11y.atspi.Application'>"
"    <method name='GetLocale'><arg name='locale' type='s' direction='out'/></method>"
"  </interface>"
"  <interface name='org.a11y.atspi.Component'>"
"    <method name='Contains'><arg name='x' type='i' direction='in'/><arg name='y' type='i' direction='in'/><arg name='coord_type' type='u' direction='in'/><arg name='contains' type='b' direction='out'/></method>"
"    <method name='GetExtents'><arg name='coord_type' type='u' direction='in'/><arg name='x' type='i' direction='out'/><arg name='y' type='i' direction='out'/><arg name='width' type='i' direction='out'/><arg name='height' type='i' direction='out'/></method>"
"    <method name='GetPosition'><arg name='coord_type' type='u' direction='in'/><arg name='x' type='i' direction='out'/><arg name='y' type='i' direction='out'/></method>"
"    <method name='GetSize'><arg name='width' type='i' direction='out'/><arg name='height' type='i' direction='out'/></method>"
"    <method name='GetLayer'><arg name='layer' type='u' direction='out'/></method>"
"    <method name='GetMDIZOrder'><arg name='order' type='n' direction='out'/></method>"
"    <method name='GrabFocus'><arg name='result' type='b' direction='out'/></method>"
"  </interface>"
"  <interface name='org.a11y.atspi.Action'>"
"    <method name='GetNActions'><arg name='count' type='i' direction='out'/></method>"
"    <method name='DoAction'><arg name='index' type='i' direction='in'/><arg name='result' type='b' direction='out'/></method>"
"    <method name='GetName'><arg name='index' type='i' direction='in'/><arg name='name' type='s' direction='out'/></method>"
"    <method name='GetDescription'><arg name='index' type='i' direction='in'/><arg name='description' type='s' direction='out'/></method>"
"    <method name='GetKeyBinding'><arg name='index' type='i' direction='in'/><arg name='binding' type='s' direction='out'/></method>"
"  </interface>"
"  <interface name='org.a11y.atspi.Value'>"
"    <property name='MinimumValue' type='d' access='read'/>"
"    <property name='CurrentValue' type='d' access='read'/>"
"    <property name='MaximumValue' type='d' access='read'/>"
"    <property name='MinimumIncrement' type='d' access='read'/>"
"    <method name='SetCurrentValue'><arg name='value' type='d' direction='in'/><arg name='result' type='b' direction='out'/></method>"
"  </interface>"
"  <interface name='org.a11y.atspi.Text'>"
"    <property name='CaretOffset' type='i' access='read'/>"
"    <property name='CharacterCount' type='i' access='read'/>"
"    <method name='GetText'><arg name='start_offset' type='i' direction='in'/><arg name='end_offset' type='i' direction='in'/><arg name='text' type='s' direction='out'/></method>"
"    <method name='SetCaretOffset'><arg name='offset' type='i' direction='in'/><arg name='result' type='b' direction='out'/></method>"
"    <method name='GetTextBeforeOffset'><arg name='offset' type='i' direction='in'/><arg name='type' type='u' direction='in'/><arg name='text' type='s' direction='out'/><arg name='start_offset' type='i' direction='out'/><arg name='end_offset' type='i' direction='out'/></method>"
"    <method name='GetTextAtOffset'><arg name='offset' type='i' direction='in'/><arg name='type' type='u' direction='in'/><arg name='text' type='s' direction='out'/><arg name='start_offset' type='i' direction='out'/><arg name='end_offset' type='i' direction='out'/></method>"
"    <method name='GetTextAfterOffset'><arg name='offset' type='i' direction='in'/><arg name='type' type='u' direction='in'/><arg name='text' type='s' direction='out'/><arg name='start_offset' type='i' direction='out'/><arg name='end_offset' type='i' direction='out'/></method>"
"    <method name='GetCharacterAtOffset'><arg name='offset' type='i' direction='in'/><arg name='character' type='i' direction='out'/></method>"
"    <method name='GetCharacterExtents'><arg name='offset' type='i' direction='in'/><arg name='coord_type' type='u' direction='in'/><arg name='x' type='i' direction='out'/><arg name='y' type='i' direction='out'/><arg name='width' type='i' direction='out'/><arg name='height' type='i' direction='out'/></method>"
"    <method name='GetRangeExtents'><arg name='start_offset' type='i' direction='in'/><arg name='end_offset' type='i' direction='in'/><arg name='coord_type' type='u' direction='in'/><arg name='rect' type='(iiii)' direction='out'/></method>"
"    <method name='GetNSelections'><arg name='count' type='i' direction='out'/></method>"
"    <method name='GetSelection'><arg name='selection_num' type='i' direction='in'/><arg name='start_offset' type='i' direction='out'/><arg name='end_offset' type='i' direction='out'/></method>"
"    <method name='SetSelection'><arg name='selection_num' type='i' direction='in'/><arg name='start_offset' type='i' direction='in'/><arg name='end_offset' type='i' direction='in'/><arg name='result' type='b' direction='out'/></method>"
"    <method name='AddSelection'><arg name='start_offset' type='i' direction='in'/><arg name='end_offset' type='i' direction='in'/><arg name='result' type='b' direction='out'/></method>"
"    <method name='RemoveSelection'><arg name='selection_num' type='i' direction='in'/><arg name='result' type='b' direction='out'/></method>"
"    <method name='GetAttributeRun'><arg name='offset' type='i' direction='in'/><arg name='include_defaults' type='b' direction='in'/><arg name='attributes' type='a{ss}' direction='out'/><arg name='start_offset' type='i' direction='out'/><arg name='end_offset' type='i' direction='out'/></method>"
"    <method name='GetDefaultAttributes'><arg name='attributes' type='a{ss}' direction='out'/></method>"
"  </interface>"
"  <interface name='org.a11y.atspi.EditableText'>"
"    <method name='SetTextContents'><arg name='contents' type='s' direction='in'/></method>"
"    <method name='InsertText'><arg name='position' type='i' direction='in'/><arg name='text' type='s' direction='in'/><arg name='length' type='i' direction='in'/></method>"
"    <method name='DeleteText'><arg name='start_pos' type='i' direction='in'/><arg name='end_pos' type='i' direction='in'/></method>"
"    <method name='ReplaceText'><arg name='start_pos' type='i' direction='in'/><arg name='end_pos' type='i' direction='in'/><arg name='text' type='s' direction='in'/></method>"
"    <method name='CopyText'><arg name='start_pos' type='i' direction='in'/><arg name='end_pos' type='i' direction='in'/></method>"
"    <method name='CutText'><arg name='start_pos' type='i' direction='in'/><arg name='end_pos' type='i' direction='in'/></method>"
"    <method name='PasteText'><arg name='position' type='i' direction='in'/></method>"
"  </interface>"
"</node>";

static GDBusInterfaceInfo *swell_atspi_find_interface(const char *name)
{
  if (!g_atspi_node_info || !name) return NULL;
  for (int i = 0; g_atspi_node_info->interfaces && g_atspi_node_info->interfaces[i]; ++i)
  {
    GDBusInterfaceInfo *iface = g_atspi_node_info->interfaces[i];
    if (iface && iface->name && !strcmp(iface->name,name)) return iface;
  }
  return NULL;
}

static SWELL_AtspiObjectRecord *swell_atspi_find_record_by_hwnd(HWND hwnd)
{
  if (!hwnd) return NULL;
  for (size_t i = 0; i < g_atspi_objects.size(); ++i)
  {
    if (g_atspi_objects[i].hwnd == hwnd) return &g_atspi_objects[i];
  }
  return NULL;
}

static SWELL_AtspiObjectRecord *swell_atspi_find_record_by_path(const char *path)
{
  if (!path) return NULL;
  for (size_t i = 0; i < g_atspi_objects.size(); ++i)
  {
    if (g_atspi_objects[i].path == path) return &g_atspi_objects[i];
  }
  return NULL;
}

static std::string swell_atspi_path_string_for_hwnd(HWND hwnd)
{
  char buf[128];
  snprintf(buf,sizeof(buf),"%s/h_%llx",SWELL_ATSPI_ACCESSIBLE_PREFIX,(unsigned long long)(uintptr_t)hwnd);
  return std::string(buf);
}

static void swell_atspi_register_hwnd(HWND hwnd)
{
  if (!hwnd) return;
  SWELL_AtspiObjectRecord *record = swell_atspi_find_record_by_hwnd(hwnd);
  if (record)
  {
    record->defunct = hwnd->m_hashaddestroy;
    if (!record->defunct) record->path = swell_atspi_path_string_for_hwnd(hwnd);
    return;
  }
  const std::string path = swell_atspi_path_string_for_hwnd(hwnd);
  record = swell_atspi_find_record_by_path(path.c_str());
  if (record)
  {
    record->hwnd = hwnd;
    record->defunct = hwnd->m_hashaddestroy;
    return;
  }
  SWELL_AtspiObjectRecord new_record;
  new_record.path = path;
  new_record.hwnd = hwnd;
  new_record.defunct = hwnd->m_hashaddestroy;
  g_atspi_objects.push_back(new_record);
}

static void swell_atspi_mark_defunct(HWND hwnd)
{
  if (!hwnd) return;
  SWELL_AtspiObjectRecord *record = swell_atspi_find_record_by_hwnd(hwnd);
  if (!record)
  {
    SWELL_AtspiObjectRecord new_record;
    new_record.path = swell_atspi_path_string_for_hwnd(hwnd);
    new_record.hwnd = NULL;
    new_record.defunct = true;
    g_atspi_objects.push_back(new_record);
    return;
  }
  record->hwnd = NULL;
  record->defunct = true;
}

static bool swell_atspi_is_live_hwnd(HWND target)
{
  SWELL_AtspiObjectRecord *record = swell_atspi_find_record_by_hwnd(target);
  return record && record->hwnd == target && !record->defunct && !target->m_hashaddestroy;
}

static HWND swell_atspi_get_root(HWND hwnd)
{
  while (hwnd && hwnd->m_parent) hwnd = hwnd->m_parent;
  return hwnd;
}

static std::string swell_atspi_path_for_hwnd(HWND hwnd)
{
  swell_atspi_register_hwnd(hwnd);
  return swell_atspi_path_string_for_hwnd(hwnd);
}

static bool swell_atspi_parse_path(const char *path, HWND *hwnd, bool *defunct)
{
  if (hwnd) *hwnd = NULL;
  if (defunct) *defunct = false;
  if (!path) return false;
  if (!strcmp(path,SWELL_ATSPI_ROOT_PATH)) return true;
  const size_t prefix_len = strlen(SWELL_ATSPI_ACCESSIBLE_PREFIX);
  if (strncmp(path,SWELL_ATSPI_ACCESSIBLE_PREFIX,prefix_len) || path[prefix_len] != '/') return false;
  const char *node = path + prefix_len + 1;
  if (strncmp(node,"h_",2)) return false;

  SWELL_AtspiObjectRecord *record = swell_atspi_find_record_by_path(path);
  if (!record) return false;
  if (record->defunct || !record->hwnd || record->hwnd->m_hashaddestroy)
  {
    record->hwnd = NULL;
    record->defunct = true;
    if (defunct) *defunct = true;
    return true;
  }
  if (hwnd) *hwnd = record->hwnd;
  return true;
}

static const char *swell_atspi_bus_name(void)
{
  const char *name = g_atspi_bus ? g_dbus_connection_get_unique_name(g_atspi_bus) : NULL;
  return name ? name : "";
}

static void swell_atspi_add_ref(GVariantBuilder *builder, const std::string &path)
{
  g_variant_builder_add(builder,"(so)",swell_atspi_bus_name(),path.c_str());
}

static void swell_atspi_add_null_ref(GVariantBuilder *builder)
{
  g_variant_builder_add(builder,"(so)","",SWELL_ATSPI_NULL_PATH);
}

static GVariant *swell_atspi_ref_variant_for_path(const std::string &path)
{
  return g_variant_new("(so)",swell_atspi_bus_name(),path.c_str());
}

static GVariant *swell_atspi_null_ref_variant(void)
{
  return g_variant_new("(so)","",SWELL_ATSPI_NULL_PATH);
}

static int swell_atspi_count_toplevel_windows(void)
{
  int count = 0;
  HWND hwnd = SWELL_topwindows;
  while (hwnd)
  {
    if (!hwnd->m_parent && !hwnd->m_hashaddestroy && hwnd->m_oswindow && hwnd->m_visible) ++count;
    hwnd = hwnd->m_next;
  }
  return count;
}

static HWND swell_atspi_toplevel_at_index(int index)
{
  HWND hwnd = SWELL_topwindows;
  while (hwnd)
  {
    if (!hwnd->m_parent && !hwnd->m_hashaddestroy && hwnd->m_oswindow && hwnd->m_visible)
    {
      if (index-- == 0) return hwnd;
    }
    hwnd = hwnd->m_next;
  }
  return NULL;
}

static int swell_atspi_count_visible_children(HWND hwnd)
{
  int count = 0;
  HWND child = hwnd ? hwnd->m_children : NULL;
  while (child)
  {
    if (!child->m_hashaddestroy && child->m_visible) ++count;
    child = child->m_next;
  }
  return count;
}

static HWND swell_atspi_child_at_index(HWND hwnd, int index)
{
  HWND child = hwnd ? hwnd->m_children : NULL;
  while (child)
  {
    if (!child->m_hashaddestroy && child->m_visible)
    {
      if (index-- == 0) return child;
    }
    child = child->m_next;
  }
  return NULL;
}

static int swell_atspi_index_in_parent(HWND hwnd)
{
  if (!hwnd) return -1;
  if (!hwnd->m_parent)
  {
    int idx = 0;
    HWND top = SWELL_topwindows;
    while (top)
    {
      if (!top->m_parent && !top->m_hashaddestroy && top->m_oswindow && top->m_visible)
      {
        if (top == hwnd) return idx;
        ++idx;
      }
      top = top->m_next;
    }
    return -1;
  }
  int idx = 0;
  HWND child = hwnd->m_parent->m_children;
  while (child)
  {
    if (!child->m_hashaddestroy && child->m_visible)
    {
      if (child == hwnd) return idx;
      ++idx;
    }
    child = child->m_next;
  }
  return -1;
}

static void swell_atspi_get_window_text(HWND hwnd, char *buf, int buflen)
{
  if (!buf || buflen < 1) return;
  buf[0] = 0;
  if (hwnd) GetWindowText(hwnd,buf,buflen);
}

static bool swell_atspi_class_is(HWND hwnd, const char *classname)
{
  return hwnd && hwnd->m_classname && classname && !stricmp(hwnd->m_classname,classname);
}

static bool swell_atspi_is_button(HWND hwnd)
{
  return swell_atspi_class_is(hwnd,"Button") && !(hwnd->m_style & BS_GROUPBOX);
}

static bool swell_atspi_is_groupbox(HWND hwnd)
{
  return swell_atspi_class_is(hwnd,"Button") && (hwnd->m_style & BS_GROUPBOX);
}

static bool swell_atspi_is_checkbox(HWND hwnd)
{
  return swell_atspi_is_button(hwnd) && ((hwnd->m_style & 0xf) == BS_AUTOCHECKBOX || (hwnd->m_style & 0xf) == BS_AUTO3STATE);
}

static bool swell_atspi_is_radio(HWND hwnd)
{
  return swell_atspi_is_button(hwnd) && ((hwnd->m_style & 0xf) == BS_AUTORADIOBUTTON);
}

static bool swell_atspi_is_edit(HWND hwnd)
{
  return swell_atspi_class_is(hwnd,"Edit");
}

static bool swell_atspi_is_slider(HWND hwnd)
{
  return swell_atspi_class_is(hwnd,"msctls_trackbar32") || swell_atspi_class_is(hwnd,"REAPERhfader");
}

static bool swell_atspi_is_progress(HWND hwnd)
{
  return swell_atspi_class_is(hwnd,"msctls_progress32");
}

static bool swell_atspi_is_combo(HWND hwnd)
{
  return swell_atspi_class_is(hwnd,"combobox") || swell_atspi_class_is(hwnd,"ComboBox");
}

static bool swell_atspi_is_list(HWND hwnd)
{
  return swell_atspi_class_is(hwnd,"ListBox") || swell_atspi_class_is(hwnd,"SysListView32_LB");
}

static bool swell_atspi_is_listview(HWND hwnd)
{
  return swell_atspi_class_is(hwnd,"SysListView32");
}

static bool swell_atspi_is_tree(HWND hwnd)
{
  return swell_atspi_class_is(hwnd,"SysTreeView32");
}

static bool swell_atspi_is_tab(HWND hwnd)
{
  return swell_atspi_class_is(hwnd,"SysTabControl32");
}

static bool swell_atspi_is_focusable(HWND hwnd)
{
  if (!hwnd || !hwnd->m_enabled) return false;
  if (swell_atspi_class_is(hwnd,"Static") || swell_atspi_is_progress(hwnd)) return false;
  return (hwnd->m_style & WS_TABSTOP) || swell_atspi_is_button(hwnd) || swell_atspi_is_edit(hwnd) ||
         swell_atspi_is_slider(hwnd) || swell_atspi_is_combo(hwnd) || swell_atspi_is_list(hwnd) ||
         swell_atspi_is_listview(hwnd) || swell_atspi_is_tree(hwnd) || swell_atspi_is_tab(hwnd);
}

static bool swell_atspi_is_focused(HWND hwnd)
{
  if (!hwnd) return false;
  HWND root = swell_atspi_get_root(hwnd);
  HWND focused = SWELL_GetFocusedChild(root);
  if (!focused) focused = GetFocus();
  return focused == hwnd;
}

static unsigned int swell_atspi_role(HWND hwnd)
{
  if (!hwnd) return SWELL_ATSPI_ROLE_APPLICATION;
  if (!hwnd->m_parent) return (hwnd->m_style & WS_CAPTION) ? SWELL_ATSPI_ROLE_FRAME : SWELL_ATSPI_ROLE_WINDOW;
  if (swell_atspi_class_is(hwnd,"Static")) return SWELL_ATSPI_ROLE_LABEL;
  if (swell_atspi_is_groupbox(hwnd)) return SWELL_ATSPI_ROLE_PANEL;
  if (swell_atspi_is_checkbox(hwnd)) return SWELL_ATSPI_ROLE_CHECK_BOX;
  if (swell_atspi_is_radio(hwnd)) return SWELL_ATSPI_ROLE_RADIO_BUTTON;
  if (swell_atspi_is_button(hwnd)) return SWELL_ATSPI_ROLE_PUSH_BUTTON;
  if (swell_atspi_is_edit(hwnd)) return (hwnd->m_style & ES_MULTILINE) ? SWELL_ATSPI_ROLE_TEXT : SWELL_ATSPI_ROLE_ENTRY;
  if (swell_atspi_is_slider(hwnd)) return SWELL_ATSPI_ROLE_SLIDER;
  if (swell_atspi_is_progress(hwnd)) return SWELL_ATSPI_ROLE_PROGRESS_BAR;
  if (swell_atspi_is_combo(hwnd)) return SWELL_ATSPI_ROLE_COMBO_BOX;
  if (swell_atspi_is_list(hwnd)) return SWELL_ATSPI_ROLE_LIST_BOX;
  if (swell_atspi_is_listview(hwnd)) return SWELL_ATSPI_ROLE_TABLE;
  if (swell_atspi_is_tree(hwnd)) return SWELL_ATSPI_ROLE_TREE;
  if (swell_atspi_is_tab(hwnd)) return SWELL_ATSPI_ROLE_PAGE_TAB_LIST;
  return (hwnd->m_style & WS_CAPTION) ? SWELL_ATSPI_ROLE_DIALOG : SWELL_ATSPI_ROLE_PANEL;
}

static const char *swell_atspi_role_name(unsigned int role)
{
  switch (role)
  {
    case SWELL_ATSPI_ROLE_APPLICATION: return "application";
    case SWELL_ATSPI_ROLE_COMBO_BOX: return "combo box";
    case SWELL_ATSPI_ROLE_FRAME: return "frame";
    case SWELL_ATSPI_ROLE_WINDOW: return "window";
    case SWELL_ATSPI_ROLE_DIALOG: return "dialog";
    case SWELL_ATSPI_ROLE_FILLER: return "filler";
    case SWELL_ATSPI_ROLE_LABEL: return "label";
    case SWELL_ATSPI_ROLE_LIST: return "list";
    case SWELL_ATSPI_ROLE_LIST_BOX: return "list box";
    case SWELL_ATSPI_ROLE_PAGE_TAB_LIST: return "page tab list";
    case SWELL_ATSPI_ROLE_PANEL: return "panel";
    case SWELL_ATSPI_ROLE_CHECK_BOX: return "check box";
    case SWELL_ATSPI_ROLE_RADIO_BUTTON: return "radio button";
    case SWELL_ATSPI_ROLE_PUSH_BUTTON: return "push button";
    case SWELL_ATSPI_ROLE_ENTRY: return "entry";
    case SWELL_ATSPI_ROLE_TEXT: return "text";
    case SWELL_ATSPI_ROLE_SLIDER: return "slider";
    case SWELL_ATSPI_ROLE_TABLE: return "table";
    case SWELL_ATSPI_ROLE_TREE: return "tree";
    case SWELL_ATSPI_ROLE_PROGRESS_BAR: return "progress bar";
  }
  return "invalid";
}

static void swell_atspi_add_state(std::vector<uint32_t> *states, int state)
{
  if (!states || state < 0) return;
  const size_t idx = (size_t)state / 32;
  while (states->size() <= idx) states->push_back(0);
  (*states)[idx] |= (uint32_t)1u << (state & 31);
}

static GVariant *swell_atspi_state_variant(HWND hwnd, bool root_object)
{
  std::vector<uint32_t> states;
  states.push_back(0);
  states.push_back(0);
  if (root_object)
  {
    swell_atspi_add_state(&states,SWELL_ATSPI_STATE_ENABLED);
    swell_atspi_add_state(&states,SWELL_ATSPI_STATE_SENSITIVE);
    swell_atspi_add_state(&states,SWELL_ATSPI_STATE_VISIBLE);
    swell_atspi_add_state(&states,SWELL_ATSPI_STATE_SHOWING);
  }
  else if (!hwnd || hwnd->m_hashaddestroy)
  {
    swell_atspi_add_state(&states,SWELL_ATSPI_STATE_DEFUNCT);
  }
  else
  {
    if (hwnd->m_enabled)
    {
      swell_atspi_add_state(&states,SWELL_ATSPI_STATE_ENABLED);
      swell_atspi_add_state(&states,SWELL_ATSPI_STATE_SENSITIVE);
    }
    if (hwnd->m_visible)
    {
      swell_atspi_add_state(&states,SWELL_ATSPI_STATE_VISIBLE);
      swell_atspi_add_state(&states,SWELL_ATSPI_STATE_SHOWING);
    }
    if (swell_atspi_is_focusable(hwnd)) swell_atspi_add_state(&states,SWELL_ATSPI_STATE_FOCUSABLE);
    if (swell_atspi_is_focused(hwnd)) swell_atspi_add_state(&states,SWELL_ATSPI_STATE_FOCUSED);
    if (swell_atspi_is_edit(hwnd) && !(hwnd->m_style & ES_READONLY))
      swell_atspi_add_state(&states,SWELL_ATSPI_STATE_EDITABLE);
    if (swell_atspi_is_edit(hwnd) && !(hwnd->m_style & ES_MULTILINE))
      swell_atspi_add_state(&states,SWELL_ATSPI_STATE_SINGLE_LINE);
    if ((swell_atspi_is_checkbox(hwnd) || swell_atspi_is_radio(hwnd)) && SendMessage(hwnd,BM_GETCHECK,0,0))
      swell_atspi_add_state(&states,SWELL_ATSPI_STATE_CHECKED);
    if (!hwnd->m_parent && hwnd->m_oswindow == SWELL_focused_oswindow)
      swell_atspi_add_state(&states,SWELL_ATSPI_STATE_ACTIVE);
  }

  GVariantBuilder builder;
  g_variant_builder_init(&builder,G_VARIANT_TYPE("au"));
  for (size_t i = 0; i < states.size(); ++i)
    g_variant_builder_add(&builder,"u",states[i]);
  return g_variant_new("(au)",&builder);
}

static void swell_atspi_get_extents(HWND hwnd, int *x, int *y, int *w, int *h)
{
  RECT r = {0,0,0,0};
  if (hwnd) GetWindowRect(hwnd,&r);
  if (x) *x = r.left;
  if (y) *y = r.top;
  if (w) *w = r.right - r.left;
  if (h) *h = r.bottom - r.top;
}

static int swell_atspi_action_count(HWND hwnd)
{
  if (!hwnd) return 0;
  if (swell_atspi_is_button(hwnd)) return 1;
  if (swell_atspi_is_focusable(hwnd)) return 1;
  return 0;
}

static const char *swell_atspi_action_name(HWND hwnd, int index)
{
  if (index != 0 || !hwnd) return "";
  if (swell_atspi_is_button(hwnd)) return "click";
  if (swell_atspi_is_combo(hwnd)) return "press";
  if (swell_atspi_is_focusable(hwnd)) return "focus";
  return "";
}

static bool swell_atspi_do_action(HWND hwnd, int index)
{
  if (index != 0 || !hwnd || hwnd->m_hashaddestroy) return false;
  if (swell_atspi_is_button(hwnd))
  {
    SetFocus(hwnd);
    SendMessage(hwnd,WM_KEYDOWN,VK_SPACE,0);
    SendMessage(hwnd,WM_KEYUP,VK_SPACE,0);
    return true;
  }
  if (swell_atspi_is_combo(hwnd))
  {
    SetFocus(hwnd);
    SendMessage(hwnd,WM_KEYDOWN,VK_SPACE,0);
    SendMessage(hwnd,WM_KEYUP,VK_SPACE,0);
    return true;
  }
  if (swell_atspi_is_focusable(hwnd))
  {
    SetFocus(hwnd);
    return true;
  }
  return false;
}

static bool swell_atspi_get_value(HWND hwnd, double *value, double *min_value, double *max_value, double *increment)
{
  if (!hwnd || !hwnd->m_private_data || (!swell_atspi_is_slider(hwnd) && !swell_atspi_is_progress(hwnd))) return false;
  int *state = (int *)hwnd->m_private_data;
  const int range = state[1];
  if (value) *value = state[0];
  if (min_value) *min_value = LOWORD(range);
  if (max_value) *max_value = HIWORD(range);
  if (increment) *increment = 1.0;
  return true;
}

static bool swell_atspi_set_value(HWND hwnd, double value)
{
  if (!hwnd || (!swell_atspi_is_slider(hwnd) && !swell_atspi_is_progress(hwnd))) return false;
  if (swell_atspi_is_slider(hwnd))
  {
    SendMessage(hwnd,TBM_SETPOS,TRUE,(LPARAM)(int)(value + 0.5));
    if (hwnd->m_parent) SendMessage(hwnd->m_parent,WM_HSCROLL,SB_ENDSCROLL,(LPARAM)hwnd);
    return true;
  }
  SendMessage(hwnd,PBM_SETPOS,(WPARAM)(int)(value + 0.5),0);
  return true;
}

static int swell_atspi_text_length(HWND hwnd)
{
  return hwnd ? WDL_utf8_get_charlen(hwnd->m_title.Get()) : 0;
}

static int swell_atspi_clamp_text_offset(HWND hwnd, int offset)
{
  const int len = swell_atspi_text_length(hwnd);
  if (offset < 0) return 0;
  return offset > len ? len : offset;
}

static std::string swell_atspi_text_range(HWND hwnd, int start_offset, int end_offset)
{
  if (!hwnd) return std::string();
  const char *text = hwnd->m_title.Get();
  const int len = swell_atspi_text_length(hwnd);
  if (end_offset < 0 || end_offset > len) end_offset = len;
  start_offset = swell_atspi_clamp_text_offset(hwnd,start_offset);
  if (end_offset < start_offset) end_offset = start_offset;
  const int start_byte = WDL_utf8_charpos_to_bytepos(text,start_offset);
  const int end_byte = WDL_utf8_charpos_to_bytepos(text,end_offset);
  return std::string(text + start_byte, end_byte - start_byte);
}

static int swell_atspi_text_character(HWND hwnd, int offset)
{
  if (!hwnd || offset < 0 || offset >= swell_atspi_text_length(hwnd)) return 0;
  const char *text = hwnd->m_title.Get();
  const int byte_offset = WDL_utf8_charpos_to_bytepos(text,offset);
  const unsigned char *p = (const unsigned char *)text + byte_offset;
  if (*p < 0x80) return *p;
  if ((*p & 0xe0) == 0xc0) return ((*p & 0x1f) << 6) | (p[1] & 0x3f);
  if ((*p & 0xf0) == 0xe0) return ((*p & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f);
  if ((*p & 0xf8) == 0xf0) return ((*p & 0x07) << 18) | ((p[1] & 0x3f) << 12) | ((p[2] & 0x3f) << 6) | (p[3] & 0x3f);
  return 0;
}

static void swell_atspi_get_text_selection(HWND hwnd, int *start, int *end)
{
  int sel_start = -1, sel_end = -1;
  swell_edit_control_get_atspi_text_state(hwnd,NULL,&sel_start,&sel_end,NULL);
  if (sel_start >= 0 && sel_end > sel_start)
  {
    if (start) *start = swell_atspi_clamp_text_offset(hwnd,sel_start);
    if (end) *end = swell_atspi_clamp_text_offset(hwnd,sel_end);
  }
  else
  {
    if (start) *start = 0;
    if (end) *end = 0;
  }
}

static bool swell_atspi_set_text_selection(HWND hwnd, int start, int end)
{
  start = swell_atspi_clamp_text_offset(hwnd,start);
  end = swell_atspi_clamp_text_offset(hwnd,end);
  return swell_edit_control_set_atspi_selection(hwnd,start,end);
}

static bool swell_atspi_replace_text(HWND hwnd, int start_offset, int end_offset, const char *insert_text)
{
  if (!swell_atspi_is_edit(hwnd) || (hwnd->m_style & ES_READONLY)) return false;
  const char *text = hwnd->m_title.Get();
  const int len = swell_atspi_text_length(hwnd);
  start_offset = swell_atspi_clamp_text_offset(hwnd,start_offset);
  if (end_offset < 0 || end_offset > len) end_offset = len;
  if (end_offset < start_offset) end_offset = start_offset;
  const int start_byte = WDL_utf8_charpos_to_bytepos(text,start_offset);
  const int end_byte = WDL_utf8_charpos_to_bytepos(text,end_offset);
  std::string value(text,start_byte);
  if (insert_text) value += insert_text;
  value += text + end_byte;
  SetWindowText(hwnd,value.c_str());
  swell_edit_control_set_atspi_selection(hwnd,start_offset + WDL_utf8_get_charlen(insert_text ? insert_text : ""),start_offset + WDL_utf8_get_charlen(insert_text ? insert_text : ""));
  return true;
}

static SWELL_AtspiInterfaceSet swell_atspi_interfaces_for_object(HWND hwnd, bool root_object)
{
  SWELL_AtspiInterfaceSet set = { true, false, false, false, false, false, false };
  if (root_object)
  {
    set.application = true;
    return set;
  }
  if (!hwnd) return set;
  set.component = true;
  set.action = swell_atspi_action_count(hwnd) > 0;
  set.value = swell_atspi_get_value(hwnd,NULL,NULL,NULL,NULL);
  set.text = swell_atspi_is_edit(hwnd);
  set.editable_text = swell_atspi_is_edit(hwnd) && !(hwnd->m_style & ES_READONLY);
  return set;
}

static void swell_atspi_add_interface_names(GVariantBuilder *builder, const SWELL_AtspiInterfaceSet &set)
{
  if (set.accessible) g_variant_builder_add(builder,"s",SWELL_ATSPI_ACCESSIBLE_IFACE);
  if (set.application) g_variant_builder_add(builder,"s",SWELL_ATSPI_APPLICATION_IFACE);
  if (set.component) g_variant_builder_add(builder,"s",SWELL_ATSPI_COMPONENT_IFACE);
  if (set.action) g_variant_builder_add(builder,"s",SWELL_ATSPI_ACTION_IFACE);
  if (set.value) g_variant_builder_add(builder,"s",SWELL_ATSPI_VALUE_IFACE);
  if (set.text) g_variant_builder_add(builder,"s",SWELL_ATSPI_TEXT_IFACE);
  if (set.editable_text) g_variant_builder_add(builder,"s",SWELL_ATSPI_EDITABLE_TEXT_IFACE);
}

static GVariant *swell_atspi_accessible_property(HWND hwnd, bool root_object, const char *property_name)
{
  if (!strcmp(property_name,"Name"))
  {
    if (root_object) return g_variant_new_string(g_swell_appname && *g_swell_appname ? g_swell_appname : "SWELL");
    char buf[1024];
    swell_atspi_get_window_text(hwnd,buf,sizeof(buf));
    return g_variant_new_string(buf);
  }
  if (!strcmp(property_name,"Description")) return g_variant_new_string("");
  if (!strcmp(property_name,"Parent"))
  {
    if (root_object) return swell_atspi_null_ref_variant();
    if (hwnd && hwnd->m_parent) return swell_atspi_ref_variant_for_path(swell_atspi_path_for_hwnd(hwnd->m_parent));
    return hwnd ? swell_atspi_ref_variant_for_path(SWELL_ATSPI_ROOT_PATH) : swell_atspi_null_ref_variant();
  }
  if (!strcmp(property_name,"ChildCount"))
  {
    int count = root_object ? swell_atspi_count_toplevel_windows() : swell_atspi_count_visible_children(hwnd);
    return g_variant_new_int32(count);
  }
  return NULL;
}

static GVariant *swell_atspi_text_property(HWND hwnd, const char *property_name)
{
  if (!swell_atspi_is_edit(hwnd)) return NULL;
  if (!strcmp(property_name,"CaretOffset"))
  {
    int cursor = 0;
    swell_edit_control_get_atspi_text_state(hwnd,&cursor,NULL,NULL,NULL);
    return g_variant_new_int32(cursor);
  }
  if (!strcmp(property_name,"CharacterCount"))
    return g_variant_new_int32(swell_atspi_text_length(hwnd));
  return NULL;
}

static GVariant *swell_atspi_value_property(HWND hwnd, const char *property_name)
{
  double value = 0.0, min_value = 0.0, max_value = 0.0, increment = 0.0;
  if (!swell_atspi_get_value(hwnd,&value,&min_value,&max_value,&increment)) return NULL;
  if (!strcmp(property_name,"CurrentValue")) return g_variant_new_double(value);
  if (!strcmp(property_name,"MinimumValue")) return g_variant_new_double(min_value);
  if (!strcmp(property_name,"MaximumValue")) return g_variant_new_double(max_value);
  if (!strcmp(property_name,"MinimumIncrement")) return g_variant_new_double(increment);
  return NULL;
}

static void swell_atspi_method_call(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
    const gchar *interface_name, const gchar *method_name, GVariant *parameters,
    GDBusMethodInvocation *invocation, gpointer user_data)
{
  (void)connection;
  (void)sender;
  (void)user_data;

  HWND hwnd = NULL;
  bool object_defunct = false;
  const bool root_object = object_path && !strcmp(object_path,SWELL_ATSPI_ROOT_PATH);
  if (!root_object && !swell_atspi_parse_path(object_path,&hwnd,&object_defunct))
  {
    g_dbus_method_invocation_return_dbus_error(invocation,"org.a11y.atspi.Error.NotFound","Object is no longer available");
    return;
  }

  if (!strcmp(interface_name,SWELL_ATSPI_ACCESSIBLE_IFACE))
  {
    if (!strcmp(method_name,"GetChildAtIndex"))
    {
      int index = -1;
      g_variant_get(parameters,"(i)",&index);
      const char *bus_name = "";
      const char *child_path = SWELL_ATSPI_NULL_PATH;
      std::string child_path_storage;
      if (root_object)
      {
        HWND child = swell_atspi_toplevel_at_index(index);
        if (child)
        {
          bus_name = swell_atspi_bus_name();
          child_path_storage = swell_atspi_path_for_hwnd(child);
          child_path = child_path_storage.c_str();
        }
      }
      else
      {
        HWND child = swell_atspi_child_at_index(hwnd,index);
        if (child)
        {
          bus_name = swell_atspi_bus_name();
          child_path_storage = swell_atspi_path_for_hwnd(child);
          child_path = child_path_storage.c_str();
        }
      }
      g_dbus_method_invocation_return_value(invocation,g_variant_new("((so))",bus_name,child_path));
      return;
    }
    if (!strcmp(method_name,"GetChildren"))
    {
      GVariantBuilder builder;
      g_variant_builder_init(&builder,G_VARIANT_TYPE("a(so)"));
      if (root_object)
      {
        for (int i = 0; ; ++i)
        {
          HWND child = swell_atspi_toplevel_at_index(i);
          if (!child) break;
          swell_atspi_add_ref(&builder,swell_atspi_path_for_hwnd(child));
        }
      }
      else
      {
        HWND child = hwnd ? hwnd->m_children : NULL;
        while (child)
        {
          if (!child->m_hashaddestroy && child->m_visible)
            swell_atspi_add_ref(&builder,swell_atspi_path_for_hwnd(child));
          child = child->m_next;
        }
      }
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(a(so))",&builder));
      return;
    }
    if (!strcmp(method_name,"GetIndexInParent"))
    {
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(i)",root_object ? -1 : swell_atspi_index_in_parent(hwnd)));
      return;
    }
    if (!strcmp(method_name,"GetRole"))
    {
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(u)",object_defunct ? SWELL_ATSPI_ROLE_INVALID : (root_object ? SWELL_ATSPI_ROLE_APPLICATION : swell_atspi_role(hwnd))));
      return;
    }
    if (!strcmp(method_name,"GetRoleName") || !strcmp(method_name,"GetLocalizedRoleName"))
    {
      const unsigned int role = object_defunct ? SWELL_ATSPI_ROLE_INVALID : (root_object ? SWELL_ATSPI_ROLE_APPLICATION : swell_atspi_role(hwnd));
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(s)",swell_atspi_role_name(role)));
      return;
    }
    if (!strcmp(method_name,"GetState"))
    {
      g_dbus_method_invocation_return_value(invocation,swell_atspi_state_variant(hwnd,root_object));
      return;
    }
    if (!strcmp(method_name,"GetRelationSet"))
    {
      GVariantBuilder builder;
      g_variant_builder_init(&builder,G_VARIANT_TYPE("a(ua(so))"));
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(a(ua(so)))",&builder));
      return;
    }
    if (!strcmp(method_name,"GetAttributes"))
    {
      GVariantBuilder builder;
      g_variant_builder_init(&builder,G_VARIANT_TYPE("a{ss}"));
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(a{ss})",&builder));
      return;
    }
    if (!strcmp(method_name,"GetApplication"))
    {
      g_dbus_method_invocation_return_value(invocation,g_variant_new("((so))",swell_atspi_bus_name(),SWELL_ATSPI_ROOT_PATH));
      return;
    }
    if (!strcmp(method_name,"GetInterfaces"))
    {
      SWELL_AtspiInterfaceSet set = swell_atspi_interfaces_for_object(hwnd,root_object);
      GVariantBuilder builder;
      g_variant_builder_init(&builder,G_VARIANT_TYPE("as"));
      swell_atspi_add_interface_names(&builder,set);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(as)",&builder));
      return;
    }
    if (!strcmp(method_name,"GetLocale"))
    {
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(s)","C"));
      return;
    }
  }
  else if (!strcmp(interface_name,SWELL_ATSPI_APPLICATION_IFACE))
  {
    if (!strcmp(method_name,"GetLocale"))
    {
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(s)","C"));
      return;
    }
  }
  else if (!strcmp(interface_name,SWELL_ATSPI_COMPONENT_IFACE))
  {
    if (!strcmp(method_name,"GetExtents"))
    {
      unsigned int coord_type = 0;
      int x = 0, y = 0, w = 0, h = 0;
      g_variant_get(parameters,"(u)",&coord_type);
      (void)coord_type;
      swell_atspi_get_extents(hwnd,&x,&y,&w,&h);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(iiii)",x,y,w,h));
      return;
    }
    if (!strcmp(method_name,"GetPosition"))
    {
      unsigned int coord_type = 0;
      int x = 0, y = 0, w = 0, h = 0;
      g_variant_get(parameters,"(u)",&coord_type);
      (void)coord_type;
      swell_atspi_get_extents(hwnd,&x,&y,&w,&h);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(ii)",x,y));
      return;
    }
    if (!strcmp(method_name,"GetSize"))
    {
      int x = 0, y = 0, w = 0, h = 0;
      swell_atspi_get_extents(hwnd,&x,&y,&w,&h);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(ii)",w,h));
      return;
    }
    if (!strcmp(method_name,"Contains"))
    {
      int x = 0, y = 0;
      unsigned int coord_type = 0;
      int rx = 0, ry = 0, rw = 0, rh = 0;
      g_variant_get(parameters,"(iiu)",&x,&y,&coord_type);
      (void)coord_type;
      swell_atspi_get_extents(hwnd,&rx,&ry,&rw,&rh);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(b)",x >= rx && x < rx + rw && y >= ry && y < ry + rh));
      return;
    }
    if (!strcmp(method_name,"GetLayer"))
    {
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(u)",0u));
      return;
    }
    if (!strcmp(method_name,"GetMDIZOrder"))
    {
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(n)",(gint16)0));
      return;
    }
    if (!strcmp(method_name,"GrabFocus"))
    {
      bool ok = hwnd && !hwnd->m_hashaddestroy;
      if (ok) SetFocus(hwnd);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(b)",ok));
      return;
    }
  }
  else if (!strcmp(interface_name,SWELL_ATSPI_ACTION_IFACE))
  {
    if (!strcmp(method_name,"GetNActions"))
    {
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(i)",swell_atspi_action_count(hwnd)));
      return;
    }
    if (!strcmp(method_name,"DoAction"))
    {
      int index = -1;
      g_variant_get(parameters,"(i)",&index);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(b)",swell_atspi_do_action(hwnd,index)));
      return;
    }
    if (!strcmp(method_name,"GetName") || !strcmp(method_name,"GetDescription") || !strcmp(method_name,"GetKeyBinding"))
    {
      int index = -1;
      g_variant_get(parameters,"(i)",&index);
      const char *value = !strcmp(method_name,"GetName") ? swell_atspi_action_name(hwnd,index) : "";
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(s)",value));
      return;
    }
  }
  else if (!strcmp(interface_name,SWELL_ATSPI_VALUE_IFACE))
  {
    if (!strcmp(method_name,"SetCurrentValue"))
    {
      double value = 0.0;
      g_variant_get(parameters,"(d)",&value);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(b)",swell_atspi_set_value(hwnd,value)));
      return;
    }
  }
  else if (!strcmp(interface_name,SWELL_ATSPI_TEXT_IFACE))
  {
    if (!strcmp(method_name,"GetText"))
    {
      int start_offset = 0, end_offset = -1;
      g_variant_get(parameters,"(ii)",&start_offset,&end_offset);
      std::string text = swell_atspi_text_range(hwnd,start_offset,end_offset);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(s)",text.c_str()));
      return;
    }
    if (!strcmp(method_name,"SetCaretOffset"))
    {
      int offset = 0;
      g_variant_get(parameters,"(i)",&offset);
      offset = swell_atspi_clamp_text_offset(hwnd,offset);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(b)",swell_atspi_set_text_selection(hwnd,offset,offset)));
      return;
    }
    if (!strcmp(method_name,"GetTextBeforeOffset") || !strcmp(method_name,"GetTextAtOffset") || !strcmp(method_name,"GetTextAfterOffset"))
    {
      int offset = 0;
      unsigned int granularity = 0;
      g_variant_get(parameters,"(iu)",&offset,&granularity);
      (void)granularity;
      int start = 0, end = swell_atspi_text_length(hwnd);
      if (!strcmp(method_name,"GetTextBeforeOffset")) end = swell_atspi_clamp_text_offset(hwnd,offset);
      else if (!strcmp(method_name,"GetTextAfterOffset")) start = swell_atspi_clamp_text_offset(hwnd,offset);
      std::string text = swell_atspi_text_range(hwnd,start,end);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(sii)",text.c_str(),start,end));
      return;
    }
    if (!strcmp(method_name,"GetCharacterAtOffset"))
    {
      int offset = 0;
      g_variant_get(parameters,"(i)",&offset);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(i)",swell_atspi_text_character(hwnd,offset)));
      return;
    }
    if (!strcmp(method_name,"GetCharacterExtents"))
    {
      int offset = 0;
      unsigned int coord_type = 0;
      int x = 0, y = 0, w = 0, h = 0;
      g_variant_get(parameters,"(iu)",&offset,&coord_type);
      (void)offset;
      (void)coord_type;
      swell_atspi_get_extents(hwnd,&x,&y,&w,&h);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(iiii)",x,y,w > 0 ? w : 1,h));
      return;
    }
    if (!strcmp(method_name,"GetRangeExtents"))
    {
      int start_offset = 0, end_offset = 0;
      unsigned int coord_type = 0;
      int x = 0, y = 0, w = 0, h = 0;
      g_variant_get(parameters,"(iiu)",&start_offset,&end_offset,&coord_type);
      (void)start_offset;
      (void)end_offset;
      (void)coord_type;
      swell_atspi_get_extents(hwnd,&x,&y,&w,&h);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("((iiii))",x,y,w,h));
      return;
    }
    if (!strcmp(method_name,"GetNSelections"))
    {
      int start = 0, end = 0;
      swell_atspi_get_text_selection(hwnd,&start,&end);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(i)",end > start ? 1 : 0));
      return;
    }
    if (!strcmp(method_name,"GetSelection"))
    {
      int selection_num = 0, start = 0, end = 0;
      g_variant_get(parameters,"(i)",&selection_num);
      if (selection_num == 0) swell_atspi_get_text_selection(hwnd,&start,&end);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(ii)",start,end));
      return;
    }
    if (!strcmp(method_name,"SetSelection"))
    {
      int selection_num = 0, start = 0, end = 0;
      g_variant_get(parameters,"(iii)",&selection_num,&start,&end);
      const bool ok = selection_num == 0 && swell_atspi_set_text_selection(hwnd,start,end);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(b)",ok));
      return;
    }
    if (!strcmp(method_name,"AddSelection"))
    {
      int start = 0, end = 0;
      g_variant_get(parameters,"(ii)",&start,&end);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(b)",swell_atspi_set_text_selection(hwnd,start,end)));
      return;
    }
    if (!strcmp(method_name,"RemoveSelection"))
    {
      int selection_num = 0, cursor = 0;
      g_variant_get(parameters,"(i)",&selection_num);
      swell_edit_control_get_atspi_text_state(hwnd,&cursor,NULL,NULL,NULL);
      cursor = swell_atspi_clamp_text_offset(hwnd,cursor);
      const bool ok = selection_num == 0 && swell_atspi_set_text_selection(hwnd,cursor,cursor);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(b)",ok));
      return;
    }
    if (!strcmp(method_name,"GetAttributeRun"))
    {
      int offset = 0;
      gboolean include_defaults = FALSE;
      GVariantBuilder builder;
      g_variant_get(parameters,"(ib)",&offset,&include_defaults);
      (void)include_defaults;
      offset = swell_atspi_clamp_text_offset(hwnd,offset);
      g_variant_builder_init(&builder,G_VARIANT_TYPE("a{ss}"));
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(a{ss}ii)",&builder,offset,offset));
      return;
    }
    if (!strcmp(method_name,"GetDefaultAttributes"))
    {
      GVariantBuilder builder;
      g_variant_builder_init(&builder,G_VARIANT_TYPE("a{ss}"));
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(a{ss})",&builder));
      return;
    }
  }
  else if (!strcmp(interface_name,SWELL_ATSPI_EDITABLE_TEXT_IFACE))
  {
    if (!strcmp(method_name,"SetTextContents"))
    {
      const char *text = "";
      g_variant_get(parameters,"(&s)",&text);
      if (swell_atspi_is_edit(hwnd) && !(hwnd->m_style & ES_READONLY))
      {
        SetWindowText(hwnd,text ? text : "");
        const int len = WDL_utf8_get_charlen(text ? text : "");
        swell_edit_control_set_atspi_selection(hwnd,len,len);
      }
      g_dbus_method_invocation_return_value(invocation,NULL);
      return;
    }
    if (!strcmp(method_name,"InsertText"))
    {
      int position = 0, length = 0;
      const char *text = "";
      g_variant_get(parameters,"(i&si)",&position,&text,&length);
      (void)length;
      swell_atspi_replace_text(hwnd,position,position,text);
      g_dbus_method_invocation_return_value(invocation,NULL);
      return;
    }
    if (!strcmp(method_name,"DeleteText"))
    {
      int start_pos = 0, end_pos = 0;
      g_variant_get(parameters,"(ii)",&start_pos,&end_pos);
      swell_atspi_replace_text(hwnd,start_pos,end_pos,"");
      g_dbus_method_invocation_return_value(invocation,NULL);
      return;
    }
    if (!strcmp(method_name,"ReplaceText"))
    {
      int start_pos = 0, end_pos = 0;
      const char *text = "";
      g_variant_get(parameters,"(ii&s)",&start_pos,&end_pos,&text);
      swell_atspi_replace_text(hwnd,start_pos,end_pos,text);
      g_dbus_method_invocation_return_value(invocation,NULL);
      return;
    }
    if (!strcmp(method_name,"CopyText") || !strcmp(method_name,"CutText") || !strcmp(method_name,"PasteText"))
    {
      g_dbus_method_invocation_return_value(invocation,NULL);
      return;
    }
  }

  g_dbus_method_invocation_return_dbus_error(invocation,"org.a11y.atspi.Error.NotSupported","Unsupported AT-SPI method");
}

static GVariant *swell_atspi_get_property(GDBusConnection *connection, const gchar *sender,
    const gchar *object_path, const gchar *interface_name, const gchar *property_name,
    GError **error, gpointer user_data)
{
  (void)connection;
  (void)sender;
  (void)user_data;

  HWND hwnd = NULL;
  bool object_defunct = false;
  const bool root_object = object_path && !strcmp(object_path,SWELL_ATSPI_ROOT_PATH);
  if (!root_object && !swell_atspi_parse_path(object_path,&hwnd,&object_defunct)) {
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Invalid object path: %s", object_path);
    return NULL;
  }
  (void)object_defunct;
  if (!strcmp(interface_name,SWELL_ATSPI_ACCESSIBLE_IFACE))
    return swell_atspi_accessible_property(hwnd,root_object,property_name);
  if (!strcmp(interface_name,SWELL_ATSPI_VALUE_IFACE))
    return swell_atspi_value_property(hwnd,property_name);
  if (!strcmp(interface_name,SWELL_ATSPI_TEXT_IFACE))
    return swell_atspi_text_property(hwnd,property_name);
  g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Unknown property: %s on interface %s", property_name, interface_name);
  return NULL;
}

static const GDBusInterfaceVTable g_atspi_interface_vtable = {
  swell_atspi_method_call,
  swell_atspi_get_property,
  NULL,
  { NULL }
};

static gchar **swell_atspi_subtree_enumerate(GDBusConnection *connection, const gchar *sender,
    const gchar *object_path, gpointer user_data)
{
  (void)connection;
  (void)sender;
  (void)object_path;
  (void)user_data;

  std::vector<std::string> nodes;
  nodes.push_back("root");
  HWND hwnd = SWELL_topwindows;
  while (hwnd)
  {
    if (!hwnd->m_hashaddestroy && hwnd->m_visible)
    {
      std::string path = swell_atspi_path_for_hwnd(hwnd);
      const char *slash = strrchr(path.c_str(),'/');
      if (slash) nodes.push_back(slash + 1);
    }
    hwnd = hwnd->m_next;
  }

  gchar **result = g_new0(gchar *,nodes.size() + 1);
  for (size_t i = 0; i < nodes.size(); ++i) result[i] = g_strdup(nodes[i].c_str());
  return result;
}

static GDBusInterfaceInfo **swell_atspi_subtree_introspect(GDBusConnection *connection,
    const gchar *sender, const gchar *object_path, const gchar *node, gpointer user_data)
{
  (void)connection;
  (void)sender;
  (void)object_path;
  (void)user_data;

  std::string path;
  if (!node) return NULL;
  path = std::string(SWELL_ATSPI_ACCESSIBLE_PREFIX) + "/" + node;

  HWND hwnd = NULL;
  const bool root_object = !strcmp(path.c_str(),SWELL_ATSPI_ROOT_PATH);
  bool object_defunct = false;
  if (!root_object && !swell_atspi_parse_path(path.c_str(),&hwnd,&object_defunct)) return NULL;
  (void)object_defunct;

  SWELL_AtspiInterfaceSet set = swell_atspi_interfaces_for_object(hwnd,root_object);
  std::vector<GDBusInterfaceInfo *> interfaces;
  if (set.accessible) interfaces.push_back(swell_atspi_find_interface(SWELL_ATSPI_ACCESSIBLE_IFACE));
  if (set.application) interfaces.push_back(swell_atspi_find_interface(SWELL_ATSPI_APPLICATION_IFACE));
  if (set.component) interfaces.push_back(swell_atspi_find_interface(SWELL_ATSPI_COMPONENT_IFACE));
  if (set.action) interfaces.push_back(swell_atspi_find_interface(SWELL_ATSPI_ACTION_IFACE));
  if (set.value) interfaces.push_back(swell_atspi_find_interface(SWELL_ATSPI_VALUE_IFACE));
  if (set.text) interfaces.push_back(swell_atspi_find_interface(SWELL_ATSPI_TEXT_IFACE));
  if (set.editable_text) interfaces.push_back(swell_atspi_find_interface(SWELL_ATSPI_EDITABLE_TEXT_IFACE));

  GDBusInterfaceInfo **result = g_new0(GDBusInterfaceInfo *,interfaces.size() + 1);
  for (size_t i = 0; i < interfaces.size(); ++i)
    result[i] = interfaces[i] ? (GDBusInterfaceInfo *)g_dbus_interface_info_ref(interfaces[i]) : NULL;
  return result;
}

static const GDBusInterfaceVTable *swell_atspi_subtree_dispatch(GDBusConnection *connection,
    const gchar *sender, const gchar *object_path, const gchar *interface_name,
    const gchar *node, gpointer *out_user_data, gpointer user_data)
{
  (void)connection;
  (void)sender;
  (void)object_path;
  (void)interface_name;
  (void)node;
  (void)user_data;
  if (out_user_data) *out_user_data = NULL;
  return &g_atspi_interface_vtable;
}

static const GDBusSubtreeVTable g_atspi_subtree_vtable = {
  swell_atspi_subtree_enumerate,
  swell_atspi_subtree_introspect,
  swell_atspi_subtree_dispatch,
  { NULL }
};

static void swell_atspi_init(void)
{
  if (g_atspi_initialized) return;
  g_atspi_initialized = true;
  g_atspi_debug = getenv("SWELL_ATSPI_DEBUG") != NULL;

  GError *error = NULL;
  GDBusConnection *session = g_bus_get_sync(G_BUS_TYPE_SESSION,NULL,&error);
  if (!session)
  {
    if (g_atspi_debug && error) fprintf(stderr,"SWELL AT-SPI session bus failed: %s\n",error->message);
    if (error) g_error_free(error);
    return;
  }

  GVariant *reply = g_dbus_connection_call_sync(session,"org.a11y.Bus","/org/a11y/bus",
      "org.a11y.Bus","GetAddress",NULL,G_VARIANT_TYPE("(s)"),G_DBUS_CALL_FLAGS_NONE,-1,NULL,&error);
  g_object_unref(session);
  if (!reply)
  {
    if (g_atspi_debug && error) fprintf(stderr,"SWELL AT-SPI GetAddress failed: %s\n",error->message);
    if (error) g_error_free(error);
    return;
  }

  char *address = NULL;
  g_variant_get(reply,"(s)",&address);
  g_variant_unref(reply);
  if (!address || !*address)
  {
    g_free(address);
    return;
  }

  g_atspi_bus = g_dbus_connection_new_for_address_sync(address,
      (GDBusConnectionFlags)(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
      NULL,NULL,&error);
  g_free(address);
  if (!g_atspi_bus)
  {
    if (g_atspi_debug && error) fprintf(stderr,"SWELL AT-SPI bus connection failed: %s\n",error->message);
    if (error) g_error_free(error);
    return;
  }

  g_atspi_node_info = g_dbus_node_info_new_for_xml(g_atspi_xml,&error);
  if (!g_atspi_node_info)
  {
    if (g_atspi_debug && error) fprintf(stderr,"SWELL AT-SPI XML parse failed: %s\n",error->message);
    if (error) g_error_free(error);
    return;
  }

  g_atspi_subtree_id = g_dbus_connection_register_subtree(g_atspi_bus,SWELL_ATSPI_ACCESSIBLE_PREFIX,
      &g_atspi_subtree_vtable,G_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES,NULL,NULL,&error);
  if (!g_atspi_subtree_id)
  {
    if (g_atspi_debug && error) fprintf(stderr,"SWELL AT-SPI subtree registration failed: %s\n",error->message);
    if (error) g_error_free(error);
    return;
  }

  reply = g_dbus_connection_call_sync(g_atspi_bus,SWELL_ATSPI_REGISTRY_NAME,SWELL_ATSPI_ROOT_PATH,
      "org.a11y.atspi.Socket","Embed",
      g_variant_new("((so))",swell_atspi_bus_name(),SWELL_ATSPI_ROOT_PATH),
      NULL,G_DBUS_CALL_FLAGS_NONE,1000,NULL,&error);
  if (reply)
  {
    g_variant_unref(reply);
    g_atspi_embedded = true;
  }
  else
  {
    if (g_atspi_debug && error) fprintf(stderr,"SWELL AT-SPI registry embed failed: %s\n",error->message);
    if (error) g_error_free(error);
  }
}

static void swell_atspi_emit_object_event(const char *path, const char *member, const char *detail, int detail1, int detail2, GVariant *value)
{
  if (!g_atspi_bus || !path || !member || !detail) return;
  GVariantBuilder props;
  g_variant_builder_init(&props,G_VARIANT_TYPE("a{sv}"));
  if (!value) value = g_variant_new_boolean(FALSE);
  g_dbus_connection_emit_signal(g_atspi_bus,NULL,path,"org.a11y.atspi.Event.Object",member,
      g_variant_new("(siiva{sv})",detail,detail1,detail2,value,&props),NULL);
}

void swell_atspi_window_created(HWND hwnd)
{
  swell_atspi_register_hwnd(hwnd);
  swell_atspi_init();
}

void swell_atspi_window_destroyed(HWND hwnd)
{
  if (!hwnd) return;
  std::string path = swell_atspi_path_for_hwnd(hwnd);
  swell_atspi_mark_defunct(hwnd);
  if (!g_atspi_bus) return;
  swell_atspi_emit_object_event(path.c_str(),"StateChanged","defunct",1,0,g_variant_new_boolean(TRUE));
}

void swell_atspi_window_changed(HWND hwnd)
{
  swell_atspi_register_hwnd(hwnd);
  if (!g_atspi_bus || !hwnd || !swell_atspi_is_live_hwnd(hwnd)) return;
  std::string path = swell_atspi_path_for_hwnd(hwnd);
  swell_atspi_emit_object_event(path.c_str(),"StateChanged","showing",hwnd->m_visible ? 1 : 0,0,g_variant_new_boolean(hwnd->m_visible));
}

void swell_atspi_focus_changed(void)
{
  if (!g_atspi_bus) return;
  HWND focused = GetFocus();
  if (focused == g_atspi_focused_hwnd) return;
  if (g_atspi_focused_hwnd && swell_atspi_is_live_hwnd(g_atspi_focused_hwnd))
  {
    std::string old_path = swell_atspi_path_for_hwnd(g_atspi_focused_hwnd);
    swell_atspi_emit_object_event(old_path.c_str(),"StateChanged","focused",0,0,g_variant_new_boolean(FALSE));
  }
  g_atspi_focused_hwnd = focused;
  if (focused && swell_atspi_is_live_hwnd(focused))
  {
    std::string new_path = swell_atspi_path_for_hwnd(focused);
    swell_atspi_emit_object_event(new_path.c_str(),"StateChanged","focused",1,0,g_variant_new_boolean(TRUE));
  }
}

void swell_atspi_pump(void)
{
  if (g_atspi_initialized && g_atspi_bus)
    swell_atspi_focus_changed();
}

void swell_atspi_keyboard_event(uint32_t event_type, uint32_t keyval, uint32_t hardware_keycode, uint32_t modifiers, int32_t timestamp, const char *event_string, bool is_text)
{
  swell_atspi_init();
  if (!g_atspi_bus) return;
  if (g_atspi_debug)
  {
    fprintf(stderr,"SWELL AT-SPI key event type=%u keyval=%u hardware=%u modifiers=%u text=%d\n",
        event_type,keyval,hardware_keycode,modifiers,is_text ? 1 : 0);
  }

  g_dbus_connection_call(g_atspi_bus,SWELL_ATSPI_REGISTRY_NAME,
      "/org/a11y/atspi/registry/deviceeventcontroller",
      "org.a11y.atspi.DeviceEventController","NotifyListenersAsync",
      g_variant_new("((uiiiisb))",event_type,(int32_t)keyval,(int32_t)hardware_keycode,
        (int32_t)modifiers,timestamp,event_string ? event_string : "",is_text),
      NULL,G_DBUS_CALL_FLAGS_NONE,100,NULL,NULL,NULL);
}

#else

void swell_atspi_window_created(HWND hwnd) { (void)hwnd; }
void swell_atspi_window_destroyed(HWND hwnd) { (void)hwnd; }
void swell_atspi_window_changed(HWND hwnd) { (void)hwnd; }
void swell_atspi_focus_changed(void) {}
void swell_atspi_pump(void) {}
void swell_atspi_keyboard_event(uint32_t event_type, uint32_t keyval, uint32_t hardware_keycode, uint32_t modifiers, int32_t timestamp, const char *event_string, bool is_text)
{
  (void)event_type;
  (void)keyval;
  (void)hardware_keycode;
  (void)modifiers;
  (void)timestamp;
  (void)event_string;
  (void)is_text;
}

#endif

#endif
