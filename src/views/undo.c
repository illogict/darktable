/*
    This file is part of darktable,
    copyright (c) 2016 pascal obry

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "views/undo.h"
#include <glib.h>    // for GList, gpointer, g_list_first, g_list_prepend
#include <stdlib.h>  // for NULL, malloc, free

typedef struct dt_undo_item_t
{
  gpointer user_data;
  dt_undo_type_t type;
  dt_undo_data_t *data;
  gboolean is_group;
  dt_undo_tag_t tag;
  void (*undo)(gpointer user_data, dt_undo_type_t type, dt_undo_data_t *data);
  void (*free_data)(gpointer data);
} dt_undo_item_t;

dt_undo_t *dt_undo_init(void)
{
  dt_undo_t *udata = malloc(sizeof(dt_undo_t));
  udata->undo_list = NULL;
  udata->redo_list = NULL;
  dt_pthread_mutex_init(&udata->mutex, NULL);
  udata->group = 0;
  return udata;
}

void dt_undo_cleanup(dt_undo_t *self)
{
  dt_undo_clear(self, DT_UNDO_ALL);
  dt_pthread_mutex_destroy(&self->mutex);
}

static void _free_undo_data(void *p)
{
  dt_undo_item_t *item = (dt_undo_item_t *)p;
  if (item->free_data) item->free_data(item->data);
  free(item);
}

static void _undo_record(dt_undo_t *self, gpointer user_data, dt_undo_type_t type, dt_undo_data_t *data,
                         gboolean is_group, dt_undo_tag_t tag,
                         void (*undo)(gpointer user_data, dt_undo_type_t type, dt_undo_data_t *item),
                         void (*free_data)(gpointer data))
{
  const GList *top = g_list_first(self->undo_list);
  const dt_undo_item_t *top_item = top == NULL ? NULL : (dt_undo_item_t *)top->data;

  if (tag == 0 || !top_item || top_item->tag != tag)
  {
    dt_undo_item_t *item = malloc(sizeof(dt_undo_item_t));

    item->user_data = user_data;
    item->type      = type;
    item->data      = data;
    item->undo      = undo;
    item->free_data = free_data;
    item->is_group  = is_group;
    item->tag       = tag;

    dt_pthread_mutex_lock(&self->mutex);
    self->undo_list = g_list_prepend(self->undo_list, (gpointer)item);

    // recording an undo data invalidate all the redo
    g_list_free_full(self->redo_list, _free_undo_data);
    self->redo_list = NULL;
    dt_pthread_mutex_unlock(&self->mutex);
  }
  else
  {
    // free the undo data as not used
    free_data(data);
  }
}

void dt_undo_start_group(dt_undo_t *self, dt_undo_type_t type)
{
  self->group = type;
  _undo_record(self, NULL, type, NULL, TRUE, 0, NULL, NULL);
}

void dt_undo_end_group(dt_undo_t *self)
{
  _undo_record(self, NULL, self->group, NULL, TRUE, 0, NULL, NULL);
  self->group = 0;
}

void dt_undo_record(dt_undo_t *self, gpointer user_data, dt_undo_type_t type, dt_undo_data_t *data, dt_undo_tag_t tag,
                    void (*undo)(gpointer user_data, dt_undo_type_t type, dt_undo_data_t *item),
                    void (*free_data)(gpointer data))
{
  _undo_record(self, user_data, type, data, FALSE, tag, undo, free_data);
}

void dt_undo_do_redo(dt_undo_t *self, uint32_t filter)
{
  dt_pthread_mutex_lock(&self->mutex);

  GList *l = g_list_first(self->redo_list);
  gboolean is_group = FALSE;
  int count=1;

  // check for first item that is matching the given pattern

  while(l)
  {
    dt_undo_item_t *item = (dt_undo_item_t *)l->data;
    GList *next = g_list_next(l);

    if(item->type & filter)
    {
      //  check if the first item is a starting group
      if (item->is_group && count==1)
        is_group = TRUE;

      //  first remove element from _redo_list
      self->redo_list = g_list_remove(self->redo_list, item);

      //  callback with redo data (except for group tag)
      if (!item->is_group)
        item->undo(item->user_data, item->type, item->data);

      //  add old position back into the undo list
      self->undo_list = g_list_prepend(self->undo_list, item);

      if (!is_group || (item->is_group && count>1))
        break;

      count++;
    }
    l = next;
  };
  dt_pthread_mutex_unlock(&self->mutex);
}

void dt_undo_do_undo(dt_undo_t *self, uint32_t filter)
{
  dt_pthread_mutex_lock(&self->mutex);

  GList *l = g_list_first(self->undo_list);

  // the first matching item (current state) is moved into the redo list

  while(l)
  {
    dt_undo_item_t *item = (dt_undo_item_t *)l->data;
    GList *next = g_list_next(l);

    if(item->type & filter)
    {
      self->undo_list = g_list_remove(self->undo_list, item);
      self->redo_list = g_list_prepend(self->redo_list, item);

      //  check if we are starting a group

      if (item->is_group)
      {
        l = next;

        // move whole goup into the redo list
        while (l)
        {
          dt_undo_item_t *g_item = (dt_undo_item_t *)l->data;
          GList *g_next = g_list_next(l);

          self->undo_list = g_list_remove(self->undo_list, g_item);
          self->redo_list = g_list_prepend(self->redo_list, g_item);

          if (g_item->is_group)
            break;

          l = g_next;
        }
      }
      break;
    }
    l = next;
  }

  // check for first item that is matching the given pattern, call undo

  l = g_list_first(self->undo_list);
  gboolean is_group = FALSE;
  int count = 1;

  while(l)
  {
    dt_undo_item_t *item = (dt_undo_item_t *)l->data;
    GList *next = g_list_next(l);

    // the second matching item (new state) is sent to callback

    if(item->type & filter)
    {
      //  check if the first item is a starting group
      if (item->is_group && count==1)
        is_group = TRUE;

      //  callback with undo data (except for group tag)
      if (!item->is_group)
        item->undo(item->user_data, item->type, item->data);

      // exit if we are not in a group, or if we reached the end of the group
      if (!is_group || (item->is_group && count>1))
        break;

      count++;
    }
    l = next;
  };
  dt_pthread_mutex_unlock(&self->mutex);
}

static void _undo_clear_list(GList **list, uint32_t filter)
{
  GList *l = g_list_first(*list);

  // check for first item that is matching the given pattern

  while(l)
  {
    dt_undo_item_t *item = (dt_undo_item_t *)l->data;
    GList *next = l->next;
    if(item->type & filter)
    {
      //  remove this element
      *list = g_list_remove(*list, item);
      _free_undo_data((void *)item);
    }
    l = next;
  };
}

void dt_undo_clear(dt_undo_t *self, uint32_t filter)
{
  dt_pthread_mutex_lock(&self->mutex);
  _undo_clear_list(&self->undo_list, filter);
  _undo_clear_list(&self->redo_list, filter);
  self->undo_list = NULL;
  self->redo_list = NULL;
  dt_pthread_mutex_unlock(&self->mutex);
}

static void _undo_iterate(GList *list, uint32_t filter, gpointer user_data,
                          void (*apply)(gpointer user_data, dt_undo_type_t type, dt_undo_data_t *item))
{
  GList *l = g_list_first(list);

  // check for first item that is matching the given pattern

  while(l)
  {
    dt_undo_item_t *item = (dt_undo_item_t *)l->data;
    if(!item->is_group && item->type & filter)
    {
      apply(user_data, item->type, item->data);
    }
    l = l->next;
  };
}

void dt_undo_iterate(dt_undo_t *self, uint32_t filter, gpointer user_data, gboolean lock,
                     void (*apply)(gpointer user_data, dt_undo_type_t type, dt_undo_data_t *item))
{
  if (lock) dt_pthread_mutex_lock(&self->mutex);
  _undo_iterate(self->undo_list, filter, user_data, apply);
  _undo_iterate(self->redo_list, filter, user_data, apply);
  if (lock) dt_pthread_mutex_unlock(&self->mutex);
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
