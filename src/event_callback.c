/* LuaEvent
   Copyright (C) 2007,2012,2013 Thomas Harning <harningt@gmail.com>

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.
   */

#include "event_callback.h"
#include <assert.h>
#include <lauxlib.h>
#include <string.h>

#define EVENT_CALLBACK_ARG_MT "EVENT_CALLBACK_ARG_MT"

void freeCallback(le_callback* cb, lua_State* L) {
	if(cb->callbackRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, cb->callbackRef);
		cb->callbackRef = LUA_NOREF;
	}
}
/* le_callback is allocated at the beginning of the coroutine in which it
is used, no need to manually de-allocate */

/* Index for coroutine is fd as integer for *nix, as lightuserdata for Win */
void luaevent_callback(int fd, short event, void* p) {
	le_callback* cb = p;
	struct event *ev = &cb->ev;
	lua_State* L;
	int ret = -1;
	struct timeval new_tv = { 0, 0 };
	if(cb->callbackRef != LUA_NOREF) {
		L = cb->base->loop_L;
		lua_rawgeti(L, LUA_REGISTRYINDEX, cb->callbackRef);
		lua_pushinteger(L, event);
		cb->running = 1;
		if(lua_pcall(L, 1, 2, 0))
		{
			cb->running = 0;
			cb->base->errorMessage = luaL_ref(L, LUA_REGISTRYINDEX);
			event_base_loopbreak(cb->base->base);
			lua_pop(L, 1); /* Pop the 'false' from pcall */
			return;
		}
		cb->running = 0;
		/* If nothing is returned, re-use the old event value */
		ret = luaL_optinteger(L, -2, event);
	}
	if(ret == -1 || cb->callbackRef == LUA_NOREF) {
		event_del(ev);
		freeCallback(cb, L);
		assert(cb->selfRef != LUA_NOREF);
		luaL_unref(L, LUA_REGISTRYINDEX, cb->selfRef);
		cb->selfRef = LUA_NOREF;
	} else if( ret != event || (cb->timeout.tv_sec != new_tv.tv_sec || cb->timeout.tv_usec != new_tv.tv_usec) ) {
		/* Clone the old timeout value in case a new one wasn't set */
		memcpy(&new_tv, &cb->timeout, sizeof(new_tv));
		if(lua_isnumber(L, -1)) {
			double newTimeout = lua_tonumber(L, -1);
			if(newTimeout > 0) {
				load_timeval(newTimeout, &new_tv);
			}
		}
		struct timeval *ptv = &cb->timeout;
		cb->timeout = new_tv;
		if(!cb->timeout.tv_sec && !cb->timeout.tv_usec)
			ptv = NULL;
		event_del(ev);
		event_set(ev, fd, EV_PERSIST | ret, luaevent_callback, cb);
		/* Assume cannot set a new timeout.. */
			event_add(ev, ptv);
	}
	lua_pop(L, 2); /* Pop two results from call */
}

static int luaevent_cb_gc(lua_State* L) {
	freeCallback(luaL_checkudata(L, 1, EVENT_CALLBACK_ARG_MT), L);
	return 0;
}

static int luaevent_cb_close(lua_State* L) {
	le_callback *cb = luaL_checkudata(L, 1, EVENT_CALLBACK_ARG_MT);
	if(!cb->running)
		event_del(&cb->ev);
	freeCallback(cb, L); // Release reference to Lua callback
	return 0;
}

le_callback* event_callback_push(lua_State* L, int baseIdx, int callbackIdx) {
	le_callback* cb;
	le_base *base = event_base_get(L, baseIdx);
	luaL_checktype(L, callbackIdx, LUA_TFUNCTION);
	cb = lua_newuserdata(L, sizeof(*cb));
	lua_pushvalue(L, -1);
	cb->selfRef = luaL_ref(L, LUA_REGISTRYINDEX);
	cb->running = 0;
	luaL_getmetatable(L, EVENT_CALLBACK_ARG_MT);
	lua_setmetatable(L, -2);

	lua_pushvalue(L, callbackIdx);
	cb->callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
	cb->base = base;
	memset(&cb->timeout, 0, sizeof(cb->timeout));
	return cb;
}

void event_callback_register(lua_State* L) {
	luaL_newmetatable(L, EVENT_CALLBACK_ARG_MT);
	lua_pushcfunction(L, luaevent_cb_gc);
	lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	lua_pushcfunction(L, luaevent_cb_close);
	lua_setfield(L, -2, "close");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
}
