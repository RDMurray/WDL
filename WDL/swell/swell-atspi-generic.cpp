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
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
static const char *SWELL_ATSPI_CACHE_PATH = "/org/a11y/atspi/cache";
static const char *SWELL_ATSPI_NULL_PATH = "/org/a11y/atspi/null";
static const char *SWELL_ATSPI_ACCESSIBLE_PREFIX = "/org/a11y/atspi/accessible";
static const char *SWELL_ATSPI_ACCESSIBLE_IFACE = "org.a11y.atspi.Accessible";
static const char *SWELL_ATSPI_APPLICATION_IFACE = "org.a11y.atspi.Application";
static const char *SWELL_ATSPI_CACHE_IFACE = "org.a11y.atspi.Cache";
static const char *SWELL_ATSPI_COLLECTION_IFACE = "org.a11y.atspi.Collection";
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
  SWELL_ATSPI_STATE_MULTI_LINE = 15,
  SWELL_ATSPI_STATE_SELECTED = 23,
  SWELL_ATSPI_STATE_SENSITIVE = 24,
  SWELL_ATSPI_STATE_SHOWING = 25,
  SWELL_ATSPI_STATE_SINGLE_LINE = 26,
  SWELL_ATSPI_STATE_VISIBLE = 29,
  SWELL_ATSPI_STATE_CHECKABLE = 41
};

enum
{
  SWELL_ATSPI_ROLE_INVALID = 0,
  SWELL_ATSPI_ROLE_CHECK_MENU_ITEM = 8,
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

enum
{
  SWELL_ATSPI_ROLE_MENU_BAR = 34,
  SWELL_ATSPI_ROLE_MENU_ITEM = 35,
  SWELL_ATSPI_ROLE_POPUP_MENU = 41,
  SWELL_ATSPI_ROLE_RADIO_MENU_ITEM = 45
};

struct SWELL_AtspiInterfaceSet
{
  bool accessible;
  bool application;
  bool component;
  bool collection;
  bool action;
  bool value;
  bool text;
  bool editable_text;
};

static GDBusConnection *g_atspi_bus;
static GDBusNodeInfo *g_atspi_node_info;
static guint g_atspi_subtree_id;
static guint g_atspi_cache_id;
static bool g_atspi_initialized;
static bool g_atspi_embedded;
static bool g_atspi_debug;
static FILE *g_atspi_log;
static uint64_t g_atspi_log_seq;
static uint64_t g_atspi_cache_calls;
static uint64_t g_atspi_cache_nodes_returned;
static std::string g_atspi_focused_path;

struct SWELL_AtspiCounter
{
  std::string key;
  uint64_t count;
};

static std::vector<SWELL_AtspiCounter> g_atspi_method_counts;
static std::vector<SWELL_AtspiCounter> g_atspi_event_counts;

struct SWELL_AtspiObjectRecord
{
  std::string path;
  HWND hwnd;
  bool defunct;
  std::string name;
  int cursor_pos;
  int sel_start;
  int sel_end;
  bool checked;
  double value;
  int selection;
  bool visible;
};

static std::vector<SWELL_AtspiObjectRecord> g_atspi_objects;

struct SWELL_AtspiMenuRecord
{
  std::string path;
  HWND menu_hwnd;
  HWND owner_hwnd;
  bool defunct;
};

static std::vector<SWELL_AtspiMenuRecord> g_atspi_menus;

static uint64_t swell_atspi_counter_increment(std::vector<SWELL_AtspiCounter> *counters, const std::string &key)
{
  if (!counters) return 0;
  for (size_t i = 0; i < counters->size(); ++i)
  {
    if ((*counters)[i].key == key)
    {
      ++(*counters)[i].count;
      return (*counters)[i].count;
    }
  }
  SWELL_AtspiCounter counter;
  counter.key = key;
  counter.count = 1;
  counters->push_back(counter);
  return 1;
}

static void swell_atspi_debug_init(void)
{
  if (!g_atspi_debug || g_atspi_log) return;
  const char *log_path = getenv("SWELL_ATSPI_LOG");
  char default_path[256];
  if (!log_path || !*log_path)
  {
    snprintf(default_path,sizeof(default_path),"/tmp/swell-atspi-%ld.log",(long)getpid());
    log_path = default_path;
  }
  g_atspi_log = fopen(log_path,"a");
  if (!g_atspi_log)
  {
    fprintf(stderr,"SWELL AT-SPI log open failed for %s: %s\n",log_path,strerror(errno));
    return;
  }
  setvbuf(g_atspi_log,NULL,_IOLBF,0);
}

static void swell_atspi_trace(const char *category, const char *fmt, ...)
{
  if (!g_atspi_debug) return;
  swell_atspi_debug_init();
  if (!g_atspi_log) return;
  const gint64 now = g_get_real_time();
  fprintf(g_atspi_log,"%" G_GINT64_FORMAT ".%06" G_GINT64_FORMAT " seq=%llu tid=%p %s ",
      now / G_GINT64_CONSTANT(1000000), now % G_GINT64_CONSTANT(1000000),
      (unsigned long long)++g_atspi_log_seq,g_thread_self(),category ? category : "trace");
  va_list args;
  va_start(args,fmt);
  vfprintf(g_atspi_log,fmt,args);
  va_end(args);
  fputc('\n',g_atspi_log);
  fflush(g_atspi_log);
}

struct SWELL_AtspiMethodTrace
{
  const char *path;
  const char *iface;
  const char *member;
  const gint64 start_us;
  const char *status;
  uint64_t count;

  SWELL_AtspiMethodTrace(const char *object_path, const char *interface_name, const char *method_name)
    : path(object_path), iface(interface_name), member(method_name), start_us(g_get_monotonic_time()), status("ok"), count(0)
  {
    std::string key = std::string(interface_name ? interface_name : "") + "." + (method_name ? method_name : "");
    count = swell_atspi_counter_increment(&g_atspi_method_counts,key);
  }

  ~SWELL_AtspiMethodTrace()
  {
    swell_atspi_trace("method","path=%s iface=%s member=%s status=%s count=%llu elapsed_us=%" G_GINT64_FORMAT,
        path ? path : "", iface ? iface : "", member ? member : "", status ? status : "",
        (unsigned long long)count,g_get_monotonic_time() - start_us);
  }
};

struct SWELL_AtspiPropertyTrace
{
  const char *path;
  const char *iface;
  const char *property;
  const gint64 start_us;
  const char *status;
  uint64_t count;

  SWELL_AtspiPropertyTrace(const char *object_path, const char *interface_name, const char *property_name)
    : path(object_path), iface(interface_name), property(property_name), start_us(g_get_monotonic_time()), status("ok"), count(0)
  {
    std::string key = std::string(interface_name ? interface_name : "") + "." + (property_name ? property_name : "");
    count = swell_atspi_counter_increment(&g_atspi_method_counts,key);
  }

  ~SWELL_AtspiPropertyTrace()
  {
    swell_atspi_trace("property","path=%s iface=%s property=%s status=%s count=%llu elapsed_us=%" G_GINT64_FORMAT,
        path ? path : "", iface ? iface : "", property ? property : "", status ? status : "",
        (unsigned long long)count,g_get_monotonic_time() - start_us);
  }
};

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
"  <interface name='org.a11y.atspi.Cache'>"
"    <property name='version' type='u' access='read'/>"
"    <method name='GetItems'><arg name='nodes' type='a((so)(so)(so)iiassusau)' direction='out'/></method>"
"    <signal name='AddAccessible'><arg name='nodeAdded' type='((so)(so)(so)iiassusau)'/></signal>"
"    <signal name='RemoveAccessible'><arg name='nodeRemoved' type='(so)'/></signal>"
"  </interface>"
"  <interface name='org.a11y.atspi.Collection'>"
"    <property name='version' type='u' access='read'/>"
"    <method name='GetMatches'><arg name='rule' type='(aiia{ss}iaiiasib)' direction='in'/><arg name='sortby' type='u' direction='in'/><arg name='count' type='i' direction='in'/><arg name='traverse' type='b' direction='in'/><arg name='matches' type='a(so)' direction='out'/></method>"
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
"    <method name='GetStringAtOffset'><arg name='offset' type='i' direction='in'/><arg name='granularity' type='u' direction='in'/><arg name='text' type='s' direction='out'/><arg name='start_offset' type='i' direction='out'/><arg name='end_offset' type='i' direction='out'/></method>"
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

static std::string swell_atspi_menubar_path_for_hwnd(HWND hwnd)
{
  char buf[128];
  snprintf(buf,sizeof(buf),"%s/mb_%llx",SWELL_ATSPI_ACCESSIBLE_PREFIX,(unsigned long long)(uintptr_t)hwnd);
  return std::string(buf);
}

static std::string swell_atspi_menubar_item_path_for_hwnd(HWND hwnd, int index)
{
  char buf[160];
  snprintf(buf,sizeof(buf),"%s/mbi_%llx_%d",SWELL_ATSPI_ACCESSIBLE_PREFIX,(unsigned long long)(uintptr_t)hwnd,index);
  return std::string(buf);
}

static std::string swell_atspi_menu_path_for_hwnd(HWND hwnd)
{
  char buf[128];
  snprintf(buf,sizeof(buf),"%s/pm_%llx",SWELL_ATSPI_ACCESSIBLE_PREFIX,(unsigned long long)(uintptr_t)hwnd);
  return std::string(buf);
}

static std::string swell_atspi_menu_item_path_for_hwnd(HWND hwnd, int index)
{
  char buf[160];
  snprintf(buf,sizeof(buf),"%s/pmi_%llx_%d",SWELL_ATSPI_ACCESSIBLE_PREFIX,(unsigned long long)(uintptr_t)hwnd,index);
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
  new_record.name = hwnd->m_title.Get();
  new_record.cursor_pos = -1;
  new_record.sel_start = -1;
  new_record.sel_end = -1;
  new_record.checked = false;
  new_record.value = 0.0;
  new_record.selection = -1;
  new_record.visible = hwnd->m_visible;
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
    new_record.cursor_pos = -1;
    new_record.sel_start = -1;
    new_record.sel_end = -1;
    new_record.checked = false;
    new_record.value = 0.0;
    new_record.selection = -1;
    new_record.visible = false;
    g_atspi_objects.push_back(new_record);
    return;
  }
  record->hwnd = NULL;
  record->defunct = true;
}

static SWELL_AtspiMenuRecord *swell_atspi_find_menu_record_by_hwnd(HWND hwnd)
{
  if (!hwnd) return NULL;
  for (size_t i = 0; i < g_atspi_menus.size(); ++i)
  {
    if (g_atspi_menus[i].menu_hwnd == hwnd) return &g_atspi_menus[i];
  }
  return NULL;
}

static SWELL_AtspiMenuRecord *swell_atspi_find_menu_record_by_path(const char *path)
{
  if (!path) return NULL;
  for (size_t i = 0; i < g_atspi_menus.size(); ++i)
  {
    if (g_atspi_menus[i].path == path) return &g_atspi_menus[i];
  }
  return NULL;
}

static void swell_atspi_register_menu(HWND menu_hwnd, HWND owner_hwnd)
{
  if (!menu_hwnd) return;
  SWELL_AtspiMenuRecord *record = swell_atspi_find_menu_record_by_hwnd(menu_hwnd);
  if (!record)
  {
    SWELL_AtspiMenuRecord new_record;
    new_record.path = swell_atspi_menu_path_for_hwnd(menu_hwnd);
    new_record.menu_hwnd = menu_hwnd;
    new_record.owner_hwnd = owner_hwnd;
    new_record.defunct = menu_hwnd->m_hashaddestroy;
    g_atspi_menus.push_back(new_record);
  }
  else
  {
    record->owner_hwnd = owner_hwnd;
    record->defunct = menu_hwnd->m_hashaddestroy;
    if (!record->defunct) record->path = swell_atspi_menu_path_for_hwnd(menu_hwnd);
  }
}

static void swell_atspi_mark_menu_defunct(HWND menu_hwnd)
{
  SWELL_AtspiMenuRecord *record = swell_atspi_find_menu_record_by_hwnd(menu_hwnd);
  if (!record) return;
  record->menu_hwnd = NULL;
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

enum SWELL_AtspiObjectKind
{
  SWELL_ATSPI_KIND_INVALID,
  SWELL_ATSPI_KIND_ROOT,
  SWELL_ATSPI_KIND_HWND,
  SWELL_ATSPI_KIND_MENUBAR,
  SWELL_ATSPI_KIND_MENUBAR_ITEM,
  SWELL_ATSPI_KIND_POPUP_MENU,
  SWELL_ATSPI_KIND_POPUP_MENU_ITEM
};

struct SWELL_AtspiResolvedObject
{
  SWELL_AtspiObjectKind kind;
  HWND hwnd;
  HWND menu_hwnd;
  HMENU menu;
  int index;
  bool defunct;
};

static bool swell_atspi_resolve_path(const char *path, SWELL_AtspiResolvedObject *obj)
{
  if (!obj) return false;
  memset(obj,0,sizeof(*obj));
  obj->kind = SWELL_ATSPI_KIND_INVALID;
  obj->index = -1;
  if (!path) return false;
  if (!strcmp(path,SWELL_ATSPI_ROOT_PATH))
  {
    obj->kind = SWELL_ATSPI_KIND_ROOT;
    return true;
  }

  const size_t prefix_len = strlen(SWELL_ATSPI_ACCESSIBLE_PREFIX);
  if (strncmp(path,SWELL_ATSPI_ACCESSIBLE_PREFIX,prefix_len) || path[prefix_len] != '/') return false;
  const char *node = path + prefix_len + 1;

  if (!strncmp(node,"h_",2))
  {
    bool defunct = false;
    HWND hwnd = NULL;
    if (!swell_atspi_parse_path(path,&hwnd,&defunct)) return false;
    obj->kind = SWELL_ATSPI_KIND_HWND;
    obj->hwnd = hwnd;
    obj->defunct = defunct;
    return true;
  }

  unsigned long long ptr = 0;
  int idx = -1;
  if (sscanf(node,"mb_%llx",&ptr) == 1)
  {
    HWND hwnd = (HWND)(uintptr_t)ptr;
    if (!swell_atspi_is_live_hwnd(hwnd) || !hwnd->m_menu) return false;
    obj->kind = SWELL_ATSPI_KIND_MENUBAR;
    obj->hwnd = hwnd;
    obj->menu = hwnd->m_menu;
    return true;
  }
  if (sscanf(node,"mbi_%llx_%d",&ptr,&idx) == 2)
  {
    HWND hwnd = (HWND)(uintptr_t)ptr;
    if (!swell_atspi_is_live_hwnd(hwnd) || !hwnd->m_menu || idx < 0 || idx >= hwnd->m_menu->items.GetSize()) return false;
    obj->kind = SWELL_ATSPI_KIND_MENUBAR_ITEM;
    obj->hwnd = hwnd;
    obj->menu = hwnd->m_menu;
    obj->index = idx;
    return true;
  }

  SWELL_AtspiMenuRecord *record = swell_atspi_find_menu_record_by_path(path);
  if (record)
  {
    obj->kind = SWELL_ATSPI_KIND_POPUP_MENU;
    obj->menu_hwnd = record->menu_hwnd;
    obj->hwnd = record->owner_hwnd;
    obj->defunct = record->defunct || !record->menu_hwnd || record->menu_hwnd->m_hashaddestroy;
    obj->menu = !obj->defunct ? (HMENU)GetWindowLongPtr(record->menu_hwnd,GWLP_USERDATA) : NULL;
    return true;
  }
  if (sscanf(node,"pmi_%llx_%d",&ptr,&idx) == 2)
  {
    HWND menu_hwnd = (HWND)(uintptr_t)ptr;
    SWELL_AtspiMenuRecord *menu_record = swell_atspi_find_menu_record_by_hwnd(menu_hwnd);
    if (!menu_record) return false;
    HMENU menu = menu_hwnd && !menu_hwnd->m_hashaddestroy ? (HMENU)GetWindowLongPtr(menu_hwnd,GWLP_USERDATA) : NULL;
    if (!menu || idx < 0 || idx >= menu->items.GetSize()) return false;
    obj->kind = SWELL_ATSPI_KIND_POPUP_MENU_ITEM;
    obj->menu_hwnd = menu_hwnd;
    obj->hwnd = menu_record->owner_hwnd;
    obj->menu = menu;
    obj->index = idx;
    obj->defunct = menu_record->defunct;
    return true;
  }
  return false;
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
    case SWELL_ATSPI_ROLE_CHECK_MENU_ITEM: return "check menu item";
    case SWELL_ATSPI_ROLE_COMBO_BOX: return "combo box";
    case SWELL_ATSPI_ROLE_FRAME: return "frame";
    case SWELL_ATSPI_ROLE_WINDOW: return "window";
    case SWELL_ATSPI_ROLE_DIALOG: return "dialog";
    case SWELL_ATSPI_ROLE_FILLER: return "filler";
    case SWELL_ATSPI_ROLE_LABEL: return "label";
    case SWELL_ATSPI_ROLE_LIST: return "list";
    case SWELL_ATSPI_ROLE_LIST_BOX: return "list box";
    case SWELL_ATSPI_ROLE_MENU_BAR: return "menu bar";
    case SWELL_ATSPI_ROLE_MENU_ITEM: return "menu item";
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
    case SWELL_ATSPI_ROLE_POPUP_MENU: return "popup menu";
    case SWELL_ATSPI_ROLE_RADIO_MENU_ITEM: return "radio menu item";
  }
  return "invalid";
}

static bool swell_atspi_menu_item_is_string(const MENUITEMINFO *inf)
{
  return inf && (inf->fType == MFT_STRING || inf->fType == MFT_RADIOCHECK);
}

static std::string swell_atspi_menu_item_name(const MENUITEMINFO *inf)
{
  std::string out;
  if (!swell_atspi_menu_item_is_string(inf) || !inf->dwTypeData) return out;
  const char *p = inf->dwTypeData;
  while (*p && *p != '\t')
  {
    if (*p == '&')
    {
      ++p;
      if (*p == '&') out.push_back(*p++);
      continue;
    }
    out.push_back(*p++);
  }
  return out;
}

static bool swell_atspi_menu_item_enabled(const MENUITEMINFO *inf)
{
  return inf && !(inf->fState & (MF_GRAYED|MF_DISABLED)) && inf->fType != MFT_SEPARATOR;
}

static bool swell_atspi_menu_item_checked(const MENUITEMINFO *inf)
{
  return inf && !!(inf->fState & MF_CHECKED);
}

static unsigned int swell_atspi_menu_item_role(const MENUITEMINFO *inf)
{
  if (inf && (inf->fType & MFT_RADIOCHECK)) return SWELL_ATSPI_ROLE_RADIO_MENU_ITEM;
  if (swell_atspi_menu_item_checked(inf)) return SWELL_ATSPI_ROLE_CHECK_MENU_ITEM;
  return SWELL_ATSPI_ROLE_MENU_ITEM;
}

static int swell_atspi_menu_visible_item_count(HMENU menu)
{
  int count = 0;
  if (menu) for (int i = 0; i < menu->items.GetSize(); ++i)
  {
    MENUITEMINFO *inf = menu->items.Get(i);
    if (inf && inf->fType != MFT_SEPARATOR) ++count;
  }
  return count;
}

static int swell_atspi_menu_visible_item_at(HMENU menu, int visible_index)
{
  if (menu) for (int i = 0; i < menu->items.GetSize(); ++i)
  {
    MENUITEMINFO *inf = menu->items.Get(i);
    if (inf && inf->fType != MFT_SEPARATOR && visible_index-- == 0) return i;
  }
  return -1;
}

static int swell_atspi_menu_item_visible_index(HMENU menu, int item_index)
{
  int visible_index = 0;
  if (menu) for (int i = 0; i < menu->items.GetSize(); ++i)
  {
    MENUITEMINFO *inf = menu->items.Get(i);
    if (!inf || inf->fType == MFT_SEPARATOR) continue;
    if (i == item_index) return visible_index;
    ++visible_index;
  }
  return -1;
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
    if (swell_atspi_is_edit(hwnd) && (hwnd->m_style & ES_MULTILINE))
      swell_atspi_add_state(&states,SWELL_ATSPI_STATE_MULTI_LINE);
    if ((swell_atspi_is_checkbox(hwnd) || swell_atspi_is_radio(hwnd)) && SendMessage(hwnd,BM_GETCHECK,0,0))
      swell_atspi_add_state(&states,SWELL_ATSPI_STATE_CHECKED);
    if (swell_atspi_is_checkbox(hwnd) || swell_atspi_is_radio(hwnd))
      swell_atspi_add_state(&states,SWELL_ATSPI_STATE_CHECKABLE);
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

static int swell_atspi_utf8_common_prefix_chars(const char *a, const char *b)
{
  int chars = 0;
  while (a && b && *a && *b)
  {
    const int alen = wdl_utf8_parsechar(a,NULL);
    const int blen = wdl_utf8_parsechar(b,NULL);
    if (alen != blen || memcmp(a,b,alen)) break;
    a += alen;
    b += blen;
    ++chars;
  }
  return chars;
}

static int swell_atspi_utf8_common_suffix_chars(const char *a, int a_chars, const char *b, int b_chars, int prefix_chars)
{
  int suffix = 0;
  while (a_chars - suffix > prefix_chars && b_chars - suffix > prefix_chars)
  {
    const int apos = WDL_utf8_charpos_to_bytepos(a,a_chars - suffix - 1);
    const int bpos = WDL_utf8_charpos_to_bytepos(b,b_chars - suffix - 1);
    const int alen = wdl_utf8_parsechar(a + apos,NULL);
    const int blen = wdl_utf8_parsechar(b + bpos,NULL);
    if (alen != blen || memcmp(a + apos,b + bpos,alen)) break;
    ++suffix;
  }
  return suffix;
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

static bool swell_atspi_text_char_is_word(HWND hwnd, int offset)
{
  const int ch = swell_atspi_text_character(hwnd,offset);
  return ch == '_' || (ch >= 0 && ch < 128 && isalnum(ch));
}

static void swell_atspi_text_line_range(HWND hwnd, int offset, int *start, int *end)
{
  const int len = swell_atspi_text_length(hwnd);
  offset = swell_atspi_clamp_text_offset(hwnd,offset);
  int s = offset, e = offset;
  while (s > 0 && swell_atspi_text_character(hwnd,s - 1) != '\n') --s;
  while (e < len && swell_atspi_text_character(hwnd,e) != '\n') ++e;
  if (start) *start = s;
  if (end) *end = e;
}

static void swell_atspi_text_word_range(HWND hwnd, int offset, int *start, int *end)
{
  const int len = swell_atspi_text_length(hwnd);
  offset = swell_atspi_clamp_text_offset(hwnd,offset);
  if (len <= 0)
  {
    if (start) *start = 0;
    if (end) *end = 0;
    return;
  }
  if (offset >= len) offset = len - 1;

  const bool want_word = swell_atspi_text_char_is_word(hwnd,offset);
  int s = offset, e = offset + 1;
  while (s > 0 && swell_atspi_text_character(hwnd,s - 1) != '\n' &&
         swell_atspi_text_char_is_word(hwnd,s - 1) == want_word) --s;
  while (e < len && swell_atspi_text_character(hwnd,e) != '\n' &&
         swell_atspi_text_char_is_word(hwnd,e) == want_word) ++e;
  if (start) *start = s;
  if (end) *end = e;
}

static void swell_atspi_text_unit_range(HWND hwnd, int offset, unsigned int granularity, bool modern, int *start, int *end)
{
  const int len = swell_atspi_text_length(hwnd);
  offset = swell_atspi_clamp_text_offset(hwnd,offset);
  if (len <= 0)
  {
    if (start) *start = 0;
    if (end) *end = 0;
    return;
  }

  const bool is_char = granularity == 0;
  const bool is_word = modern ? granularity == 1 : (granularity == 1 || granularity == 2);
  const bool is_line = modern ? granularity == 3 : (granularity == 5 || granularity == 6);
  if (is_char)
  {
    if (offset >= len) offset = len - 1;
    if (start) *start = offset;
    if (end) *end = offset + 1;
  }
  else if (is_word)
  {
    swell_atspi_text_word_range(hwnd,offset,start,end);
  }
  else if (is_line)
  {
    swell_atspi_text_line_range(hwnd,offset,start,end);
  }
  else
  {
    if (start) *start = 0;
    if (end) *end = len;
  }
}

static void swell_atspi_text_offset_range(HWND hwnd, const char *method_name, int offset, unsigned int granularity, bool modern, int *start, int *end)
{
  const int len = swell_atspi_text_length(hwnd);
  offset = swell_atspi_clamp_text_offset(hwnd,offset);
  int s = 0, e = 0;
  swell_atspi_text_unit_range(hwnd,offset,granularity,modern,&s,&e);

  if (!modern && method_name && !strcmp(method_name,"GetTextBeforeOffset"))
  {
    if (s <= 0) s = e = 0;
    else swell_atspi_text_unit_range(hwnd,s - 1,granularity,false,&s,&e);
  }
  else if (!modern && method_name && !strcmp(method_name,"GetTextAfterOffset"))
  {
    if (e >= len) s = e = len;
    else swell_atspi_text_unit_range(hwnd,e,granularity,false,&s,&e);
  }

  if (start) *start = s;
  if (end) *end = e;
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
  SWELL_AtspiInterfaceSet set = { true, false, false, false, false, false, false, false };
  if (root_object)
  {
    set.application = true;
    set.collection = true;
    return set;
  }
  if (!hwnd) return set;
  set.component = true;
  set.collection = swell_atspi_count_visible_children(hwnd) > 0 || (!hwnd->m_parent && hwnd->m_menu);
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
  if (set.collection) g_variant_builder_add(builder,"s",SWELL_ATSPI_COLLECTION_IFACE);
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
    if (swell_atspi_is_edit(hwnd)) return g_variant_new_string("");
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

static std::string swell_atspi_path_for_resolved(const SWELL_AtspiResolvedObject &obj)
{
  switch (obj.kind)
  {
    case SWELL_ATSPI_KIND_ROOT: return SWELL_ATSPI_ROOT_PATH;
    case SWELL_ATSPI_KIND_HWND: return swell_atspi_path_for_hwnd(obj.hwnd);
    case SWELL_ATSPI_KIND_MENUBAR: return swell_atspi_menubar_path_for_hwnd(obj.hwnd);
    case SWELL_ATSPI_KIND_MENUBAR_ITEM: return swell_atspi_menubar_item_path_for_hwnd(obj.hwnd,obj.index);
    case SWELL_ATSPI_KIND_POPUP_MENU: return swell_atspi_menu_path_for_hwnd(obj.menu_hwnd);
    case SWELL_ATSPI_KIND_POPUP_MENU_ITEM: return swell_atspi_menu_item_path_for_hwnd(obj.menu_hwnd,obj.index);
    default: break;
  }
  return SWELL_ATSPI_NULL_PATH;
}

static MENUITEMINFO *swell_atspi_resolved_menu_item(const SWELL_AtspiResolvedObject &obj)
{
  return obj.menu && obj.index >= 0 ? obj.menu->items.Get(obj.index) : NULL;
}

static std::string swell_atspi_name_for_resolved(const SWELL_AtspiResolvedObject &obj)
{
  if (obj.kind == SWELL_ATSPI_KIND_ROOT)
    return g_swell_appname && *g_swell_appname ? g_swell_appname : "SWELL";
  if (obj.kind == SWELL_ATSPI_KIND_HWND && swell_atspi_is_edit(obj.hwnd))
    return std::string();
  if (obj.kind == SWELL_ATSPI_KIND_HWND)
  {
    char buf[1024];
    swell_atspi_get_window_text(obj.hwnd,buf,sizeof(buf));
    return buf;
  }
  if (obj.kind == SWELL_ATSPI_KIND_MENUBAR) return "Menu Bar";
  if (obj.kind == SWELL_ATSPI_KIND_POPUP_MENU) return "Menu";
  if (obj.kind == SWELL_ATSPI_KIND_MENUBAR_ITEM || obj.kind == SWELL_ATSPI_KIND_POPUP_MENU_ITEM)
    return swell_atspi_menu_item_name(swell_atspi_resolved_menu_item(obj));
  return std::string();
}

static unsigned int swell_atspi_role_for_resolved(const SWELL_AtspiResolvedObject &obj)
{
  if (obj.defunct) return SWELL_ATSPI_ROLE_INVALID;
  switch (obj.kind)
  {
    case SWELL_ATSPI_KIND_ROOT: return SWELL_ATSPI_ROLE_APPLICATION;
    case SWELL_ATSPI_KIND_HWND: return swell_atspi_role(obj.hwnd);
    case SWELL_ATSPI_KIND_MENUBAR: return SWELL_ATSPI_ROLE_MENU_BAR;
    case SWELL_ATSPI_KIND_POPUP_MENU: return SWELL_ATSPI_ROLE_POPUP_MENU;
    case SWELL_ATSPI_KIND_MENUBAR_ITEM:
    case SWELL_ATSPI_KIND_POPUP_MENU_ITEM:
      return swell_atspi_menu_item_role(swell_atspi_resolved_menu_item(obj));
    default: break;
  }
  return SWELL_ATSPI_ROLE_INVALID;
}

static int swell_atspi_child_count_for_resolved(const SWELL_AtspiResolvedObject &obj)
{
  if (obj.defunct) return -1;
  if (obj.kind == SWELL_ATSPI_KIND_ROOT) return swell_atspi_count_toplevel_windows();
  if (obj.kind == SWELL_ATSPI_KIND_HWND)
    return swell_atspi_count_visible_children(obj.hwnd) + (!obj.hwnd->m_parent && obj.hwnd->m_menu ? 1 : 0);
  if (obj.kind == SWELL_ATSPI_KIND_MENUBAR || obj.kind == SWELL_ATSPI_KIND_POPUP_MENU)
    return swell_atspi_menu_visible_item_count(obj.menu);
  if ((obj.kind == SWELL_ATSPI_KIND_MENUBAR_ITEM || obj.kind == SWELL_ATSPI_KIND_POPUP_MENU_ITEM) &&
      swell_atspi_resolved_menu_item(obj) && swell_atspi_resolved_menu_item(obj)->hSubMenu)
    return 0;
  return 0;
}

static bool swell_atspi_child_ref_for_resolved(const SWELL_AtspiResolvedObject &obj, int index, std::string *path)
{
  if (!path || index < 0 || obj.defunct) return false;
  if (obj.kind == SWELL_ATSPI_KIND_ROOT)
  {
    HWND child = swell_atspi_toplevel_at_index(index);
    if (!child) return false;
    *path = swell_atspi_path_for_hwnd(child);
    return true;
  }
  if (obj.kind == SWELL_ATSPI_KIND_HWND)
  {
    if (!obj.hwnd->m_parent && obj.hwnd->m_menu)
    {
      if (index == 0)
      {
        *path = swell_atspi_menubar_path_for_hwnd(obj.hwnd);
        return true;
      }
      --index;
    }
    HWND child = swell_atspi_child_at_index(obj.hwnd,index);
    if (!child) return false;
    *path = swell_atspi_path_for_hwnd(child);
    return true;
  }
  if (obj.kind == SWELL_ATSPI_KIND_MENUBAR || obj.kind == SWELL_ATSPI_KIND_POPUP_MENU)
  {
    const int item_index = swell_atspi_menu_visible_item_at(obj.menu,index);
    if (item_index < 0) return false;
    *path = obj.kind == SWELL_ATSPI_KIND_MENUBAR ?
      swell_atspi_menubar_item_path_for_hwnd(obj.hwnd,item_index) :
      swell_atspi_menu_item_path_for_hwnd(obj.menu_hwnd,item_index);
    return true;
  }
  return false;
}

static int swell_atspi_index_for_resolved(const SWELL_AtspiResolvedObject &obj)
{
  if (obj.kind == SWELL_ATSPI_KIND_ROOT) return -1;
  if (obj.kind == SWELL_ATSPI_KIND_HWND)
  {
    int idx = swell_atspi_index_in_parent(obj.hwnd);
    if (obj.hwnd && obj.hwnd->m_parent && !obj.hwnd->m_parent->m_parent && obj.hwnd->m_parent->m_menu && idx >= 0) ++idx;
    return idx;
  }
  if (obj.kind == SWELL_ATSPI_KIND_MENUBAR) return 0;
  if (obj.kind == SWELL_ATSPI_KIND_MENUBAR_ITEM || obj.kind == SWELL_ATSPI_KIND_POPUP_MENU_ITEM)
    return swell_atspi_menu_item_visible_index(obj.menu,obj.index);
  return -1;
}

static GVariant *swell_atspi_parent_variant_for_resolved(const SWELL_AtspiResolvedObject &obj)
{
  if (obj.kind == SWELL_ATSPI_KIND_ROOT) return swell_atspi_null_ref_variant();
  if (obj.kind == SWELL_ATSPI_KIND_HWND)
  {
    if (obj.hwnd && obj.hwnd->m_parent) return swell_atspi_ref_variant_for_path(swell_atspi_path_for_hwnd(obj.hwnd->m_parent));
    return swell_atspi_ref_variant_for_path(SWELL_ATSPI_ROOT_PATH);
  }
  if (obj.kind == SWELL_ATSPI_KIND_MENUBAR) return swell_atspi_ref_variant_for_path(swell_atspi_path_for_hwnd(obj.hwnd));
  if (obj.kind == SWELL_ATSPI_KIND_MENUBAR_ITEM) return swell_atspi_ref_variant_for_path(swell_atspi_menubar_path_for_hwnd(obj.hwnd));
  if (obj.kind == SWELL_ATSPI_KIND_POPUP_MENU)
  {
    if (obj.hwnd) return swell_atspi_ref_variant_for_path(swell_atspi_path_for_hwnd(obj.hwnd));
    return swell_atspi_ref_variant_for_path(SWELL_ATSPI_ROOT_PATH);
  }
  if (obj.kind == SWELL_ATSPI_KIND_POPUP_MENU_ITEM) return swell_atspi_ref_variant_for_path(swell_atspi_menu_path_for_hwnd(obj.menu_hwnd));
  return swell_atspi_null_ref_variant();
}

static bool swell_atspi_is_resolved_focused(const SWELL_AtspiResolvedObject &obj)
{
  if (obj.defunct) return false;
  if (obj.kind == SWELL_ATSPI_KIND_HWND) return swell_atspi_is_focused(obj.hwnd);
  if (obj.kind == SWELL_ATSPI_KIND_MENUBAR_ITEM) return obj.menu && obj.menu->sel_vis == obj.index;
  if (obj.kind == SWELL_ATSPI_KIND_POPUP_MENU_ITEM) return obj.menu && obj.menu->sel_vis == obj.index;
  return false;
}

static GVariant *swell_atspi_state_variant_for_resolved(const SWELL_AtspiResolvedObject &obj)
{
  if (obj.kind == SWELL_ATSPI_KIND_ROOT) return swell_atspi_state_variant(NULL,true);
  if (obj.kind == SWELL_ATSPI_KIND_HWND) return swell_atspi_state_variant(obj.hwnd,false);

  std::vector<uint32_t> states;
  states.push_back(0);
  states.push_back(0);
  if (obj.defunct)
  {
    swell_atspi_add_state(&states,SWELL_ATSPI_STATE_DEFUNCT);
  }
  else
  {
    MENUITEMINFO *inf = swell_atspi_resolved_menu_item(obj);
    const bool is_item = obj.kind == SWELL_ATSPI_KIND_MENUBAR_ITEM || obj.kind == SWELL_ATSPI_KIND_POPUP_MENU_ITEM;
    const bool enabled = !is_item || swell_atspi_menu_item_enabled(inf);
    if (enabled)
    {
      swell_atspi_add_state(&states,SWELL_ATSPI_STATE_ENABLED);
      swell_atspi_add_state(&states,SWELL_ATSPI_STATE_SENSITIVE);
    }
    swell_atspi_add_state(&states,SWELL_ATSPI_STATE_VISIBLE);
    swell_atspi_add_state(&states,SWELL_ATSPI_STATE_SHOWING);
    if (is_item) swell_atspi_add_state(&states,SWELL_ATSPI_STATE_FOCUSABLE);
    if (swell_atspi_is_resolved_focused(obj)) swell_atspi_add_state(&states,SWELL_ATSPI_STATE_FOCUSED);
    if (is_item && (swell_atspi_menu_item_checked(inf) || (inf && (inf->fType & MFT_RADIOCHECK))))
      swell_atspi_add_state(&states,SWELL_ATSPI_STATE_CHECKABLE);
    if (is_item && swell_atspi_menu_item_checked(inf))
      swell_atspi_add_state(&states,SWELL_ATSPI_STATE_CHECKED);
  }
  GVariantBuilder builder;
  g_variant_builder_init(&builder,G_VARIANT_TYPE("au"));
  for (size_t i = 0; i < states.size(); ++i)
    g_variant_builder_add(&builder,"u",states[i]);
  return g_variant_new("(au)",&builder);
}

static SWELL_AtspiInterfaceSet swell_atspi_interfaces_for_resolved(const SWELL_AtspiResolvedObject &obj)
{
  if (obj.kind == SWELL_ATSPI_KIND_ROOT) return swell_atspi_interfaces_for_object(NULL,true);
  if (obj.kind == SWELL_ATSPI_KIND_HWND) return swell_atspi_interfaces_for_object(obj.hwnd,false);
  SWELL_AtspiInterfaceSet set = { true, false, true, false, false, false, false, false };
  if (obj.kind == SWELL_ATSPI_KIND_MENUBAR_ITEM || obj.kind == SWELL_ATSPI_KIND_POPUP_MENU_ITEM)
    set.action = swell_atspi_menu_item_enabled(swell_atspi_resolved_menu_item(obj));
  return set;
}

static void swell_atspi_cache_add_item(GVariantBuilder *nodes, const SWELL_AtspiResolvedObject &obj)
{
  if (!nodes) return;
  std::string path = swell_atspi_path_for_resolved(obj);
  GVariant *parent = swell_atspi_parent_variant_for_resolved(obj);
  const char *parent_bus = "";
  const char *parent_path = SWELL_ATSPI_NULL_PATH;
  g_variant_get(parent,"(&s&o)",&parent_bus,&parent_path);

  SWELL_AtspiInterfaceSet set = swell_atspi_interfaces_for_resolved(obj);
  GVariantBuilder ifaces;
  g_variant_builder_init(&ifaces,G_VARIANT_TYPE("as"));
  swell_atspi_add_interface_names(&ifaces,set);

  GVariant *state_tuple = swell_atspi_state_variant_for_resolved(obj);
  GVariant *state_array = g_variant_get_child_value(state_tuple,0);

  GVariantBuilder states;
  g_variant_builder_init(&states,G_VARIANT_TYPE("au"));
  GVariantIter iter;
  guint32 state_value = 0;
  g_variant_iter_init(&iter,state_array);
  while (g_variant_iter_next(&iter,"u",&state_value))
    g_variant_builder_add(&states,"u",state_value);

  std::string name = swell_atspi_name_for_resolved(obj);
  int cache_index = swell_atspi_index_for_resolved(obj);
  int cache_child_count = swell_atspi_child_count_for_resolved(obj);
  if (obj.kind == SWELL_ATSPI_KIND_POPUP_MENU || obj.kind == SWELL_ATSPI_KIND_POPUP_MENU_ITEM)
    cache_index = -1;
  g_variant_builder_add(nodes,"((so)(so)(so)iiassusau)",
    swell_atspi_bus_name(),path.c_str(),
    swell_atspi_bus_name(),SWELL_ATSPI_ROOT_PATH,
    parent_bus,parent_path,
    cache_index,
    cache_child_count,
    &ifaces,
    name.c_str(),
    swell_atspi_role_for_resolved(obj),
    swell_atspi_role_name(swell_atspi_role_for_resolved(obj)),
    &states);

  g_variant_unref(state_array);
  g_variant_unref(state_tuple);
  g_variant_unref(parent);
}

static void swell_atspi_cache_add_tree(GVariantBuilder *nodes, const SWELL_AtspiResolvedObject &obj)
{
  swell_atspi_cache_add_item(nodes,obj);
  const int count = swell_atspi_child_count_for_resolved(obj);
  for (int i = 0; i < count; ++i)
  {
    std::string child_path;
    SWELL_AtspiResolvedObject child;
    if (swell_atspi_child_ref_for_resolved(obj,i,&child_path) && swell_atspi_resolve_path(child_path.c_str(),&child))
      swell_atspi_cache_add_tree(nodes,child);
  }
}

static GVariant *swell_atspi_cache_items_variant(void)
{
  GVariantBuilder nodes;
  g_variant_builder_init(&nodes,G_VARIANT_TYPE("a((so)(so)(so)iiassusau)"));
  int node_count = 0;
  SWELL_AtspiResolvedObject root;
  if (swell_atspi_resolve_path(SWELL_ATSPI_ROOT_PATH,&root))
  {
    swell_atspi_cache_add_tree(&nodes,root);
    node_count = 1;
    for (size_t i = 0; i < g_atspi_objects.size(); ++i)
      if (!g_atspi_objects[i].defunct && swell_atspi_is_live_hwnd(g_atspi_objects[i].hwnd) && g_atspi_objects[i].hwnd->m_visible)
        ++node_count;
  }
  for (size_t i = 0; i < g_atspi_menus.size(); ++i)
  {
    if (!g_atspi_menus[i].defunct && g_atspi_menus[i].menu_hwnd)
    {
      SWELL_AtspiResolvedObject menu;
      if (swell_atspi_resolve_path(g_atspi_menus[i].path.c_str(),&menu))
      {
        swell_atspi_cache_add_tree(&nodes,menu);
        node_count += swell_atspi_child_count_for_resolved(menu) + 1;
      }
    }
  }
  ++g_atspi_cache_calls;
  g_atspi_cache_nodes_returned += node_count;
  swell_atspi_trace("cache","GetItems nodes=%d calls=%llu total_nodes=%llu",
      node_count,(unsigned long long)g_atspi_cache_calls,(unsigned long long)g_atspi_cache_nodes_returned);
  return g_variant_new("(a((so)(so)(so)iiassusau))",&nodes);
}

static GVariant *swell_atspi_cache_single_item_variant(const SWELL_AtspiResolvedObject &obj)
{
  GVariantBuilder nodes;
  g_variant_builder_init(&nodes,G_VARIANT_TYPE("a((so)(so)(so)iiassusau)"));
  swell_atspi_cache_add_item(&nodes,obj);
  GVariant *array = g_variant_builder_end(&nodes);
  GVariant *item = g_variant_get_child_value(array,0);
  g_variant_unref(array);
  return item;
}

static void swell_atspi_emit_cache_add(const SWELL_AtspiResolvedObject &obj)
{
  if (!g_atspi_bus) return;
  GVariant *item = swell_atspi_cache_single_item_variant(obj);
  g_dbus_connection_emit_signal(g_atspi_bus,NULL,SWELL_ATSPI_CACHE_PATH,SWELL_ATSPI_CACHE_IFACE,"AddAccessible",
      g_variant_new("(@((so)(so)(so)iiassusau))",item),NULL);
  swell_atspi_counter_increment(&g_atspi_event_counts,"Cache.AddAccessible");
  swell_atspi_trace("event","path=%s iface=%s member=AddAccessible",
      swell_atspi_path_for_resolved(obj).c_str(),SWELL_ATSPI_CACHE_IFACE);
}

static void swell_atspi_emit_cache_remove(const char *path)
{
  if (!g_atspi_bus || !path) return;
  g_dbus_connection_emit_signal(g_atspi_bus,NULL,SWELL_ATSPI_CACHE_PATH,SWELL_ATSPI_CACHE_IFACE,"RemoveAccessible",
      g_variant_new("((so))",swell_atspi_bus_name(),path),NULL);
  swell_atspi_counter_increment(&g_atspi_event_counts,"Cache.RemoveAccessible");
  swell_atspi_trace("event","path=%s iface=%s member=RemoveAccessible",path,SWELL_ATSPI_CACHE_IFACE);
}

static bool swell_atspi_bitset_contains(const std::vector<int> &bits, unsigned int value)
{
  const size_t idx = value / 32;
  return idx < bits.size() && (((uint32_t)bits[idx] >> (value & 31)) & 1u);
}

static bool swell_atspi_match_bitset(bool has_any_rule, bool any_match, bool all_match, int match_type)
{
  switch (match_type)
  {
    case 1: return !has_any_rule || all_match;      // ALL
    case 2: return !has_any_rule || any_match;      // ANY
    case 3: return !has_any_rule || !any_match;     // NONE
    case 4: return !has_any_rule || all_match;      // EMPTY
    default: return true;
  }
}

static bool swell_atspi_match_strings(bool has_any_rule, bool any_match, bool all_match, int match_type)
{
  return swell_atspi_match_bitset(has_any_rule,any_match,all_match,match_type);
}

static bool swell_atspi_interface_matches_rule(const char *iface, const SWELL_AtspiInterfaceSet &set)
{
  if (!iface || !*iface) return false;
  if (!strcmp(iface,SWELL_ATSPI_ACCESSIBLE_IFACE) || !strcmp(iface,"Accessible")) return set.accessible;
  if (!strcmp(iface,SWELL_ATSPI_APPLICATION_IFACE) || !strcmp(iface,"Application")) return set.application;
  if (!strcmp(iface,SWELL_ATSPI_COLLECTION_IFACE) || !strcmp(iface,"Collection")) return set.collection;
  if (!strcmp(iface,SWELL_ATSPI_COMPONENT_IFACE) || !strcmp(iface,"Component")) return set.component;
  if (!strcmp(iface,SWELL_ATSPI_ACTION_IFACE) || !strcmp(iface,"Action")) return set.action;
  if (!strcmp(iface,SWELL_ATSPI_VALUE_IFACE) || !strcmp(iface,"Value")) return set.value;
  if (!strcmp(iface,SWELL_ATSPI_TEXT_IFACE) || !strcmp(iface,"Text")) return set.text;
  if (!strcmp(iface,SWELL_ATSPI_EDITABLE_TEXT_IFACE) || !strcmp(iface,"EditableText")) return set.editable_text;
  return false;
}

static bool swell_atspi_collection_rule_matches(const SWELL_AtspiResolvedObject &obj,
    const std::vector<int> &state_bits, int state_match,
    const std::vector<int> &role_bits, int role_match,
    const std::vector<std::string> &interfaces, int interface_match,
    bool invert)
{
  const unsigned int role = swell_atspi_role_for_resolved(obj);
  const bool has_role_rule = !role_bits.empty();
  const bool role_any = swell_atspi_bitset_contains(role_bits,role);
  if (!swell_atspi_match_bitset(has_role_rule,role_any,role_any,role_match)) return invert;

  bool state_any = false;
  bool state_all = true;
  bool has_state_rule = false;
  GVariant *state_tuple = swell_atspi_state_variant_for_resolved(obj);
  GVariant *state_array = g_variant_get_child_value(state_tuple,0);
  std::vector<int> actual_states;
  GVariantIter state_iter;
  guint32 state_word = 0;
  g_variant_iter_init(&state_iter,state_array);
  while (g_variant_iter_next(&state_iter,"u",&state_word))
    actual_states.push_back((int)state_word);
  for (size_t word = 0; word < state_bits.size(); ++word)
  {
    uint32_t requested = (uint32_t)state_bits[word];
    if (!requested) continue;
    has_state_rule = true;
    const uint32_t actual = word < actual_states.size() ? (uint32_t)actual_states[word] : 0;
    if (actual & requested) state_any = true;
    if ((actual & requested) != requested) state_all = false;
  }
  g_variant_unref(state_array);
  g_variant_unref(state_tuple);
  if (!swell_atspi_match_bitset(has_state_rule,state_any,state_all,state_match)) return invert;

  SWELL_AtspiInterfaceSet set = swell_atspi_interfaces_for_resolved(obj);
  bool interface_any = false;
  bool interface_all = true;
  for (size_t i = 0; i < interfaces.size(); ++i)
  {
    const bool ok = swell_atspi_interface_matches_rule(interfaces[i].c_str(),set);
    interface_any = interface_any || ok;
    interface_all = interface_all && ok;
  }
  if (!swell_atspi_match_strings(!interfaces.empty(),interface_any,interface_all,interface_match)) return invert;
  return !invert;
}

static void swell_atspi_collection_add_matches(GVariantBuilder *builder, const SWELL_AtspiResolvedObject &obj,
    const std::vector<int> &state_bits, int state_match,
    const std::vector<int> &role_bits, int role_match,
    const std::vector<std::string> &interfaces, int interface_match,
    bool invert, int *remaining, bool traverse)
{
  if (!builder || (remaining && *remaining == 0)) return;
  const int count = swell_atspi_child_count_for_resolved(obj);
  for (int i = 0; i < count; ++i)
  {
    std::string child_path;
    SWELL_AtspiResolvedObject child;
    if (!swell_atspi_child_ref_for_resolved(obj,i,&child_path) || !swell_atspi_resolve_path(child_path.c_str(),&child))
      continue;
    if (swell_atspi_collection_rule_matches(child,state_bits,state_match,role_bits,role_match,interfaces,interface_match,invert))
    {
      swell_atspi_add_ref(builder,child_path);
      if (remaining && *remaining > 0 && --*remaining == 0) return;
    }
    if (traverse)
      swell_atspi_collection_add_matches(builder,child,state_bits,state_match,role_bits,role_match,interfaces,interface_match,invert,remaining,traverse);
    if (remaining && *remaining == 0) return;
  }
}

static void swell_atspi_method_call(GDBusConnection *connection, const gchar *sender, const gchar *object_path,
    const gchar *interface_name, const gchar *method_name, GVariant *parameters,
    GDBusMethodInvocation *invocation, gpointer user_data)
{
  (void)connection;
  (void)sender;
  (void)user_data;
  SWELL_AtspiMethodTrace method_trace(object_path,interface_name,method_name);

  if (!strcmp(interface_name,SWELL_ATSPI_CACHE_IFACE))
  {
    if (!strcmp(method_name,"GetItems"))
    {
      g_dbus_method_invocation_return_value(invocation,swell_atspi_cache_items_variant());
      return;
    }
    g_dbus_method_invocation_return_dbus_error(invocation,"org.a11y.atspi.Error.NotSupported","Unsupported AT-SPI cache method");
    return;
  }

  SWELL_AtspiResolvedObject obj;
  if (!swell_atspi_resolve_path(object_path,&obj))
  {
    method_trace.status = "not-found";
    swell_atspi_trace("resolve","path=%s status=not-found iface=%s member=%s",
        object_path ? object_path : "",interface_name ? interface_name : "",method_name ? method_name : "");
    g_dbus_method_invocation_return_dbus_error(invocation,"org.a11y.atspi.Error.NotFound","Object is no longer available");
    return;
  }
  HWND hwnd = obj.hwnd;

  if (!strcmp(interface_name,SWELL_ATSPI_ACCESSIBLE_IFACE))
  {
    if (!strcmp(method_name,"GetChildAtIndex"))
    {
      int index = -1;
      g_variant_get(parameters,"(i)",&index);
      const char *bus_name = "";
      const char *child_path = SWELL_ATSPI_NULL_PATH;
      std::string child_path_storage;
      if (swell_atspi_child_ref_for_resolved(obj,index,&child_path_storage))
      {
        bus_name = swell_atspi_bus_name();
        child_path = child_path_storage.c_str();
      }
      g_dbus_method_invocation_return_value(invocation,g_variant_new("((so))",bus_name,child_path));
      return;
    }
    if (!strcmp(method_name,"GetChildren"))
    {
      GVariantBuilder builder;
      g_variant_builder_init(&builder,G_VARIANT_TYPE("a(so)"));
      const int count = swell_atspi_child_count_for_resolved(obj);
      for (int i = 0; i < count; ++i)
      {
        std::string child_path;
        if (swell_atspi_child_ref_for_resolved(obj,i,&child_path))
          swell_atspi_add_ref(&builder,child_path);
      }
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(a(so))",&builder));
      return;
    }
    if (!strcmp(method_name,"GetIndexInParent"))
    {
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(i)",swell_atspi_index_for_resolved(obj)));
      return;
    }
    if (!strcmp(method_name,"GetRole"))
    {
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(u)",swell_atspi_role_for_resolved(obj)));
      return;
    }
    if (!strcmp(method_name,"GetRoleName") || !strcmp(method_name,"GetLocalizedRoleName"))
    {
      const unsigned int role = swell_atspi_role_for_resolved(obj);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(s)",swell_atspi_role_name(role)));
      return;
    }
    if (!strcmp(method_name,"GetState"))
    {
      g_dbus_method_invocation_return_value(invocation,swell_atspi_state_variant_for_resolved(obj));
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
      g_variant_builder_add(&builder,"{ss}","toolkit","SWELL");
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
      SWELL_AtspiInterfaceSet set = swell_atspi_interfaces_for_resolved(obj);
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
  else if (!strcmp(interface_name,SWELL_ATSPI_COLLECTION_IFACE))
  {
    if (!strcmp(method_name,"GetMatches"))
    {
      unsigned int sortby = 0;
      int count = 0;
      gboolean traverse = FALSE;
      g_variant_get_child(parameters,1,"u",&sortby);
      g_variant_get_child(parameters,2,"i",&count);
      g_variant_get_child(parameters,3,"b",&traverse);
      (void)sortby;

      std::vector<int> state_bits;
      std::vector<int> role_bits;
      std::vector<std::string> interfaces;
      int state_match = 1, role_match = 1, interface_match = 1;
      gboolean invert = FALSE;

      GVariant *rule = g_variant_get_child_value(parameters,0);
      GVariant *states = g_variant_get_child_value(rule,0);
      GVariantIter iter;
      gint32 bit_word = 0;
      g_variant_iter_init(&iter,states);
      while (g_variant_iter_next(&iter,"i",&bit_word)) state_bits.push_back(bit_word);
      g_variant_get_child(rule,1,"i",&state_match);
      GVariant *roles = g_variant_get_child_value(rule,4);
      g_variant_iter_init(&iter,roles);
      while (g_variant_iter_next(&iter,"i",&bit_word)) role_bits.push_back(bit_word);
      g_variant_get_child(rule,5,"i",&role_match);
      GVariant *ifaces = g_variant_get_child_value(rule,6);
      const char *iface = NULL;
      g_variant_iter_init(&iter,ifaces);
      while (g_variant_iter_next(&iter,"&s",&iface)) interfaces.push_back(iface ? iface : "");
      g_variant_get_child(rule,7,"i",&interface_match);
      g_variant_get_child(rule,8,"b",&invert);
      g_variant_unref(ifaces);
      g_variant_unref(roles);
      g_variant_unref(states);
      g_variant_unref(rule);

      int remaining = count;
      GVariantBuilder builder;
      g_variant_builder_init(&builder,G_VARIANT_TYPE("a(so)"));
      swell_atspi_collection_add_matches(&builder,obj,state_bits,state_match,role_bits,role_match,
          interfaces,interface_match,invert,count > 0 ? &remaining : NULL,traverse);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(a(so))",&builder));
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
      int count = swell_atspi_action_count(hwnd);
      if (obj.kind == SWELL_ATSPI_KIND_MENUBAR_ITEM || obj.kind == SWELL_ATSPI_KIND_POPUP_MENU_ITEM)
        count = swell_atspi_menu_item_enabled(swell_atspi_resolved_menu_item(obj)) ? 1 : 0;
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(i)",count));
      return;
    }
    if (!strcmp(method_name,"DoAction"))
    {
      int index = -1;
      g_variant_get(parameters,"(i)",&index);
      bool ok = false;
      MENUITEMINFO *inf = swell_atspi_resolved_menu_item(obj);
      if (index == 0 && inf && swell_atspi_menu_item_enabled(inf))
      {
        if (obj.kind == SWELL_ATSPI_KIND_POPUP_MENU_ITEM && obj.menu_hwnd)
        {
          SendMessage(obj.menu_hwnd,WM_USER+100,1,obj.index);
          ok = true;
        }
        else if (obj.kind == SWELL_ATSPI_KIND_MENUBAR_ITEM && obj.hwnd)
        {
          if (inf->hSubMenu)
          {
            RECT r;
            GetWindowRect(obj.hwnd,&r);
            TrackPopupMenu(inf->hSubMenu,0,r.left,r.top,0,obj.hwnd,NULL);
            ok = true;
          }
          else if (inf->wID)
          {
            SendMessage(obj.hwnd,WM_COMMAND,inf->wID,0);
            ok = true;
          }
        }
      }
      else ok = swell_atspi_do_action(hwnd,index);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(b)",ok));
      return;
    }
    if (!strcmp(method_name,"GetName") || !strcmp(method_name,"GetDescription") || !strcmp(method_name,"GetKeyBinding"))
    {
      int index = -1;
      g_variant_get(parameters,"(i)",&index);
      const char *value = "";
      if (!strcmp(method_name,"GetName"))
        value = (obj.kind == SWELL_ATSPI_KIND_MENUBAR_ITEM || obj.kind == SWELL_ATSPI_KIND_POPUP_MENU_ITEM) && index == 0 ? "click" : swell_atspi_action_name(hwnd,index);
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
      int start = 0, end = 0;
      swell_atspi_text_offset_range(hwnd,method_name,offset,granularity,false,&start,&end);
      std::string text = swell_atspi_text_range(hwnd,start,end);
      g_dbus_method_invocation_return_value(invocation,g_variant_new("(sii)",text.c_str(),start,end));
      return;
    }
    if (!strcmp(method_name,"GetStringAtOffset"))
    {
      int offset = 0;
      unsigned int granularity = 0;
      g_variant_get(parameters,"(iu)",&offset,&granularity);
      int start = 0, end = 0;
      swell_atspi_text_offset_range(hwnd,method_name,offset,granularity,true,&start,&end);
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

  method_trace.status = "unsupported";
  g_dbus_method_invocation_return_dbus_error(invocation,"org.a11y.atspi.Error.NotSupported","Unsupported AT-SPI method");
}

static GVariant *swell_atspi_get_property(GDBusConnection *connection, const gchar *sender,
    const gchar *object_path, const gchar *interface_name, const gchar *property_name,
    GError **error, gpointer user_data)
{
  (void)connection;
  (void)sender;
  (void)user_data;
  SWELL_AtspiPropertyTrace property_trace(object_path,interface_name,property_name);

  if (!strcmp(interface_name,SWELL_ATSPI_CACHE_IFACE))
  {
    if (!strcmp(property_name,"version"))
    {
      return g_variant_new_uint32(1);
    }
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Unknown cache property: %s", property_name);
    property_trace.status = "unknown";
    return NULL;
  }

  SWELL_AtspiResolvedObject obj;
  if (!swell_atspi_resolve_path(object_path,&obj)) {
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Invalid object path: %s", object_path);
    property_trace.status = "not-found";
    swell_atspi_trace("resolve","path=%s status=not-found iface=%s property=%s",
        object_path ? object_path : "",interface_name ? interface_name : "",property_name ? property_name : "");
    return NULL;
  }
  HWND hwnd = obj.hwnd;
  const bool root_object = obj.kind == SWELL_ATSPI_KIND_ROOT;
  if (!strcmp(interface_name,SWELL_ATSPI_ACCESSIBLE_IFACE))
  {
    if (!strcmp(property_name,"Name")) return g_variant_new_string(swell_atspi_name_for_resolved(obj).c_str());
    if (!strcmp(property_name,"Description")) return g_variant_new_string("");
    if (!strcmp(property_name,"Parent")) return swell_atspi_parent_variant_for_resolved(obj);
    if (!strcmp(property_name,"ChildCount")) return g_variant_new_int32(swell_atspi_child_count_for_resolved(obj));
    return swell_atspi_accessible_property(hwnd,root_object,property_name);
  }
  if (!strcmp(interface_name,SWELL_ATSPI_VALUE_IFACE))
    return swell_atspi_value_property(hwnd,property_name);
  if (!strcmp(interface_name,SWELL_ATSPI_TEXT_IFACE))
    return swell_atspi_text_property(hwnd,property_name);
  if (!strcmp(interface_name,SWELL_ATSPI_COLLECTION_IFACE) && !strcmp(property_name,"version"))
    return g_variant_new_uint32(1);
  g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Unknown property: %s on interface %s", property_name, interface_name);
  property_trace.status = "unknown";
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

  SWELL_AtspiResolvedObject obj;
  if (!swell_atspi_resolve_path(path.c_str(),&obj)) return NULL;

  SWELL_AtspiInterfaceSet set = swell_atspi_interfaces_for_resolved(obj);
  std::vector<GDBusInterfaceInfo *> interfaces;
  if (set.accessible) interfaces.push_back(swell_atspi_find_interface(SWELL_ATSPI_ACCESSIBLE_IFACE));
  if (set.application) interfaces.push_back(swell_atspi_find_interface(SWELL_ATSPI_APPLICATION_IFACE));
  if (set.collection) interfaces.push_back(swell_atspi_find_interface(SWELL_ATSPI_COLLECTION_IFACE));
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
  swell_atspi_debug_init();
  swell_atspi_trace("init","debug=1");

  GError *error = NULL;
  GDBusConnection *session = g_bus_get_sync(G_BUS_TYPE_SESSION,NULL,&error);
  if (!session)
  {
    if (g_atspi_debug && error) fprintf(stderr,"SWELL AT-SPI session bus failed: %s\n",error->message);
    if (error) swell_atspi_trace("init","session-bus status=failed error=%s",error->message);
    if (error) g_error_free(error);
    return;
  }

  GVariant *reply = g_dbus_connection_call_sync(session,"org.a11y.Bus","/org/a11y/bus",
      "org.a11y.Bus","GetAddress",NULL,G_VARIANT_TYPE("(s)"),G_DBUS_CALL_FLAGS_NONE,-1,NULL,&error);
  g_object_unref(session);
  if (!reply)
  {
    if (g_atspi_debug && error) fprintf(stderr,"SWELL AT-SPI GetAddress failed: %s\n",error->message);
    if (error) swell_atspi_trace("init","GetAddress status=failed error=%s",error->message);
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
    if (error) swell_atspi_trace("init","atspi-bus status=failed error=%s",error->message);
    if (error) g_error_free(error);
    return;
  }

  g_atspi_node_info = g_dbus_node_info_new_for_xml(g_atspi_xml,&error);
  if (!g_atspi_node_info)
  {
    if (g_atspi_debug && error) fprintf(stderr,"SWELL AT-SPI XML parse failed: %s\n",error->message);
    if (error) swell_atspi_trace("init","xml status=failed error=%s",error->message);
    if (error) g_error_free(error);
    return;
  }

  g_atspi_subtree_id = g_dbus_connection_register_subtree(g_atspi_bus,SWELL_ATSPI_ACCESSIBLE_PREFIX,
      &g_atspi_subtree_vtable,G_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES,NULL,NULL,&error);
  if (!g_atspi_subtree_id)
  {
    if (g_atspi_debug && error) fprintf(stderr,"SWELL AT-SPI subtree registration failed: %s\n",error->message);
    if (error) swell_atspi_trace("init","subtree status=failed error=%s",error->message);
    if (error) g_error_free(error);
    return;
  }

  GDBusInterfaceInfo *cache_iface = swell_atspi_find_interface(SWELL_ATSPI_CACHE_IFACE);
  if (cache_iface)
  {
    g_atspi_cache_id = g_dbus_connection_register_object(g_atspi_bus,SWELL_ATSPI_CACHE_PATH,
        cache_iface,&g_atspi_interface_vtable,NULL,NULL,&error);
    if (!g_atspi_cache_id)
    {
      if (g_atspi_debug && error) fprintf(stderr,"SWELL AT-SPI cache registration failed: %s\n",error->message);
      if (error) swell_atspi_trace("init","cache-registration status=failed error=%s",error->message);
      if (error) { g_error_free(error); error = NULL; }
    }
  }

  reply = g_dbus_connection_call_sync(g_atspi_bus,SWELL_ATSPI_REGISTRY_NAME,SWELL_ATSPI_ROOT_PATH,
      "org.a11y.atspi.Socket","Embed",
      g_variant_new("((so))",swell_atspi_bus_name(),SWELL_ATSPI_ROOT_PATH),
      NULL,G_DBUS_CALL_FLAGS_NONE,1000,NULL,&error);
  if (reply)
  {
    g_variant_unref(reply);
    g_atspi_embedded = true;
    swell_atspi_trace("init","registry-embed status=ok bus=%s",swell_atspi_bus_name());
  }
  else
  {
    if (g_atspi_debug && error) fprintf(stderr,"SWELL AT-SPI registry embed failed: %s\n",error->message);
    if (error) swell_atspi_trace("init","registry-embed status=failed error=%s",error->message);
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
  std::string key = std::string("Object.") + member + "." + detail;
  swell_atspi_counter_increment(&g_atspi_event_counts,key);
  swell_atspi_trace("event","path=%s iface=org.a11y.atspi.Event.Object member=%s detail=%s detail1=%d detail2=%d",
      path,member,detail,detail1,detail2);
}

static void swell_atspi_emit_state_changed(const char *path, const char *detail, bool enabled)
{
  swell_atspi_emit_object_event(path,"StateChanged",detail,enabled ? 1 : 0,0,g_variant_new_string("0"));
}

static void swell_atspi_emit_focus_event(const char *path)
{
  if (!g_atspi_bus || !path) return;
  GVariantBuilder props;
  g_variant_builder_init(&props,G_VARIANT_TYPE("a{sv}"));
  g_dbus_connection_emit_signal(g_atspi_bus,NULL,path,"org.a11y.atspi.Event.Focus","Focus",
      g_variant_new("(siiva{sv})","",0,0,g_variant_new_boolean(TRUE),&props),NULL);
  swell_atspi_counter_increment(&g_atspi_event_counts,"Focus.Focus");
  swell_atspi_trace("event","path=%s iface=org.a11y.atspi.Event.Focus member=Focus",path);
}

static void swell_atspi_focus_path_changed(const std::string &new_path)
{
  if (!g_atspi_bus || new_path == g_atspi_focused_path) return;
  if (!g_atspi_focused_path.empty())
    swell_atspi_emit_state_changed(g_atspi_focused_path.c_str(),"focused",false);
  g_atspi_focused_path = new_path;
  swell_atspi_trace("focus","path=%s",g_atspi_focused_path.c_str());
  if (!g_atspi_focused_path.empty())
  {
    swell_atspi_emit_state_changed(g_atspi_focused_path.c_str(),"focused",true);
  }
}

void swell_atspi_window_created(HWND hwnd)
{
  const bool was_initialized = g_atspi_initialized;
  swell_atspi_register_hwnd(hwnd);
  swell_atspi_init();
  if (!was_initialized || !g_atspi_bus || !hwnd || hwnd->m_hashaddestroy || !hwnd->m_parent) return;
  std::string path = swell_atspi_path_for_hwnd(hwnd);
  SWELL_AtspiResolvedObject obj;
  if (swell_atspi_resolve_path(path.c_str(),&obj))
    swell_atspi_emit_cache_add(obj);
  swell_atspi_trace("window","created path=%s hwnd=%p",path.c_str(),hwnd);
}

void swell_atspi_window_destroyed(HWND hwnd)
{
  if (!hwnd) return;
  std::string path = swell_atspi_path_for_hwnd(hwnd);
  swell_atspi_mark_defunct(hwnd);
  if (!g_atspi_bus) return;
  swell_atspi_emit_state_changed(path.c_str(),"defunct",true);
  swell_atspi_emit_cache_remove(path.c_str());
  swell_atspi_trace("window","destroyed path=%s hwnd=%p",path.c_str(),hwnd);
}

void swell_atspi_window_changed(HWND hwnd)
{
  swell_atspi_register_hwnd(hwnd);
  if (!g_atspi_bus || !hwnd || !swell_atspi_is_live_hwnd(hwnd)) return;
  std::string path = swell_atspi_path_for_hwnd(hwnd);
  SWELL_AtspiObjectRecord *record = swell_atspi_find_record_by_hwnd(hwnd);
  if (!record) return;

  const std::string old_name = record->name;
  const std::string new_name = hwnd->m_title.Get();
  if (old_name != new_name)
  {
    if (swell_atspi_is_edit(hwnd) || swell_atspi_is_combo(hwnd))
    {
      const int old_chars = WDL_utf8_get_charlen(old_name.c_str());
      const int new_chars = WDL_utf8_get_charlen(new_name.c_str());
      const int prefix = swell_atspi_utf8_common_prefix_chars(old_name.c_str(),new_name.c_str());
      const int suffix = swell_atspi_utf8_common_suffix_chars(old_name.c_str(),old_chars,new_name.c_str(),new_chars,prefix);
      if (new_chars > old_chars)
      {
        std::string inserted = swell_atspi_text_range(hwnd,prefix,new_chars - suffix);
        swell_atspi_emit_object_event(path.c_str(),"TextChanged","insert",prefix,new_chars - old_chars,
            g_variant_new_string(inserted.c_str()));
      }
      else if (old_chars > new_chars)
      {
        const int start_byte = WDL_utf8_charpos_to_bytepos(old_name.c_str(),prefix);
        const int end_byte = WDL_utf8_charpos_to_bytepos(old_name.c_str(),old_chars - suffix);
        std::string deleted(old_name.c_str() + start_byte,end_byte - start_byte);
        swell_atspi_emit_object_event(path.c_str(),"TextChanged","delete",prefix,old_chars - new_chars,
            g_variant_new_string(deleted.c_str()));
      }
      else
      {
        swell_atspi_emit_object_event(path.c_str(),"TextChanged","",prefix,0,g_variant_new_string(new_name.c_str()));
      }
    }
    if (!swell_atspi_is_edit(hwnd))
      swell_atspi_emit_object_event(path.c_str(),"PropertyChange","accessible-name",0,0,g_variant_new_string(new_name.c_str()));
    record->name = new_name;
  }

  if (swell_atspi_is_edit(hwnd) || swell_atspi_is_combo(hwnd))
  {
    int cursor = -1, sel_start = -1, sel_end = -1;
    if (swell_edit_control_get_atspi_text_state(hwnd,&cursor,&sel_start,&sel_end,NULL))
    {
      if (cursor != record->cursor_pos)
      {
        swell_atspi_emit_object_event(path.c_str(),"TextCaretMoved","",cursor,0,g_variant_new_int32(0));
        record->cursor_pos = cursor;
      }
      if (sel_start != record->sel_start || sel_end != record->sel_end)
      {
        swell_atspi_emit_object_event(path.c_str(),"TextSelectionChanged","",0,0,g_variant_new_string(""));
        record->sel_start = sel_start;
        record->sel_end = sel_end;
      }
    }
  }

  if (swell_atspi_is_checkbox(hwnd) || swell_atspi_is_radio(hwnd))
  {
    const bool checked = SendMessage(hwnd,BM_GETCHECK,0,0) != 0;
    if (checked != record->checked)
    {
      record->checked = checked;
      swell_atspi_emit_state_changed(path.c_str(),"checked",checked);
    }
  }

  if (swell_atspi_is_slider(hwnd) || swell_atspi_is_progress(hwnd))
  {
    double value = 0.0, min_value = 0.0, max_value = 0.0, increment = 0.0;
    if (swell_atspi_get_value(hwnd,&value,&min_value,&max_value,&increment) && value != record->value)
    {
      record->value = value;
      swell_atspi_emit_object_event(path.c_str(),"PropertyChange","accessible-value",0,0,g_variant_new_double(value));
    }
  }

  if (swell_atspi_is_combo(hwnd) || swell_atspi_is_list(hwnd))
  {
    const int selection = (int)SendMessage(hwnd,swell_atspi_is_combo(hwnd) ? CB_GETCURSEL : LB_GETCURSEL,0,0);
    if (selection != record->selection)
    {
      record->selection = selection;
      swell_atspi_emit_object_event(path.c_str(),"SelectionChanged","",selection,0,NULL);
    }
  }

  const bool visible = hwnd->m_visible;
  if (visible != record->visible)
  {
    record->visible = visible;
    swell_atspi_emit_state_changed(path.c_str(),"showing",visible);
  }
  swell_atspi_trace("window","changed path=%s visible=%d",path.c_str(),hwnd->m_visible ? 1 : 0);
}

void swell_atspi_focus_changed(void)
{
  swell_atspi_init();
  if (!g_atspi_bus) return;
  HWND focused = GetFocus();
  if (focused && swell_atspi_is_live_hwnd(focused))
    swell_atspi_focus_path_changed(swell_atspi_path_for_hwnd(focused));
  else
    swell_atspi_focus_path_changed(std::string());
}

void swell_atspi_focus_changed_to(HWND hwnd)
{
  swell_atspi_register_hwnd(hwnd);
  swell_atspi_init();
  if (!g_atspi_bus) return;
  if (hwnd && swell_atspi_is_live_hwnd(hwnd))
    swell_atspi_focus_path_changed(swell_atspi_path_for_hwnd(hwnd));
  else
    swell_atspi_focus_changed();
}

void swell_atspi_menu_created(HWND menu_hwnd, HWND owner_hwnd)
{
  swell_atspi_register_menu(menu_hwnd,owner_hwnd);
  swell_atspi_init();
  if (!g_atspi_bus || !menu_hwnd) return;
  std::string path = swell_atspi_menu_path_for_hwnd(menu_hwnd);
  SWELL_AtspiResolvedObject obj;
  if (swell_atspi_resolve_path(path.c_str(),&obj))
    swell_atspi_emit_cache_add(obj);
  swell_atspi_emit_object_event(path.c_str(),"ChildrenChanged","add",0,0,swell_atspi_ref_variant_for_path(path));
  swell_atspi_menu_selection_changed(menu_hwnd);
  swell_atspi_trace("menu","created path=%s owner=%p",path.c_str(),owner_hwnd);
}

void swell_atspi_menu_destroyed(HWND menu_hwnd)
{
  if (!menu_hwnd) return;
  std::string path = swell_atspi_menu_path_for_hwnd(menu_hwnd);
  swell_atspi_mark_menu_defunct(menu_hwnd);
  if (!g_atspi_bus) return;
  if (g_atspi_focused_path == path || g_atspi_focused_path.find(path + "_") == 0)
    swell_atspi_focus_path_changed(std::string());
  swell_atspi_emit_state_changed(path.c_str(),"defunct",true);
  swell_atspi_emit_cache_remove(path.c_str());
  swell_atspi_trace("menu","destroyed path=%s",path.c_str());
}

void swell_atspi_menu_selection_changed(HWND menu_hwnd)
{
  swell_atspi_init();
  SWELL_AtspiMenuRecord *record = swell_atspi_find_menu_record_by_hwnd(menu_hwnd);
  HMENU menu = menu_hwnd && !menu_hwnd->m_hashaddestroy ? (HMENU)GetWindowLongPtr(menu_hwnd,GWLP_USERDATA) : NULL;
  if (!g_atspi_bus || !record || !menu) return;
  std::string menu_path = swell_atspi_menu_path_for_hwnd(menu_hwnd);
  swell_atspi_emit_object_event(menu_path.c_str(),"SelectionChanged","",0,0,NULL);
  swell_atspi_trace("menu","selection path=%s index=%d",menu_path.c_str(),menu->sel_vis);
  if (menu->sel_vis >= 0 && menu->sel_vis < menu->items.GetSize())
    swell_atspi_focus_path_changed(swell_atspi_menu_item_path_for_hwnd(menu_hwnd,menu->sel_vis));
}

void swell_atspi_menubar_selection_changed(HWND owner_hwnd)
{
  swell_atspi_init();
  if (!g_atspi_bus || !owner_hwnd || !owner_hwnd->m_menu) return;
  std::string menu_path = swell_atspi_menubar_path_for_hwnd(owner_hwnd);
  swell_atspi_emit_object_event(menu_path.c_str(),"SelectionChanged","",0,0,NULL);
  swell_atspi_trace("menu","menubar-selection path=%s index=%d",menu_path.c_str(),owner_hwnd->m_menu->sel_vis);
  if (owner_hwnd->m_menu->sel_vis >= 0 && owner_hwnd->m_menu->sel_vis < owner_hwnd->m_menu->items.GetSize())
    swell_atspi_focus_path_changed(swell_atspi_menubar_item_path_for_hwnd(owner_hwnd,owner_hwnd->m_menu->sel_vis));
}

void swell_atspi_pump(void)
{
  if (g_atspi_focused_path.find("/pmi_") != std::string::npos ||
      g_atspi_focused_path.find("/mbi_") != std::string::npos)
    return;
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
  swell_atspi_trace("keyboard","type=%u keyval=%u hardware=%u modifiers=%u timestamp=%d text=%d string=%s",
      event_type,keyval,hardware_keycode,modifiers,(int)timestamp,is_text ? 1 : 0,event_string ? event_string : "");

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
void swell_atspi_focus_changed_to(HWND hwnd) { (void)hwnd; }
void swell_atspi_pump(void) {}
void swell_atspi_menu_created(HWND menu_hwnd, HWND owner_hwnd) { (void)menu_hwnd; (void)owner_hwnd; }
void swell_atspi_menu_destroyed(HWND menu_hwnd) { (void)menu_hwnd; }
void swell_atspi_menu_selection_changed(HWND menu_hwnd) { (void)menu_hwnd; }
void swell_atspi_menubar_selection_changed(HWND owner_hwnd) { (void)owner_hwnd; }
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
