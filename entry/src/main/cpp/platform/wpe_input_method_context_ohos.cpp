/**
 * Copyright (C) 2026 Jani Hautakangas <jani@kodegood.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "platform/wpe_input_method_context_ohos.h"

#include "log.h"

#include <inputmethod/inputmethod_controller_capi.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>

// Editor-proxy callbacks only receive the InputMethod_TextEditorProxy pointer; map it back to
// the owning context. Callbacks run on the main thread (SetCallbackInMainThread), so the map and
// the emitted signals need no locking.
static std::unordered_map<InputMethod_TextEditorProxy*, WPEInputMethodContextOHOS*>& proxyToContextMap()
{
    static std::unordered_map<InputMethod_TextEditorProxy*, WPEInputMethodContextOHOS*> map;
    return map;
}

struct _WPEInputMethodContextOHOS {
    WPEInputMethodContext parent;

    InputMethod_TextEditorProxy* editorProxy;
    InputMethod_InputMethodProxy* imProxy;

    char* preedit;
    int preeditCursor;

    char* surroundingText;
    unsigned surroundingCursorIndex;
    unsigned surroundingSelectionIndex;

    struct {
        int x;
        int y;
        int width;
        int height;
    } cursorRect;
};

G_DEFINE_FINAL_TYPE(WPEInputMethodContextOHOS, wpe_input_method_context_ohos, WPE_TYPE_INPUT_METHOD_CONTEXT)

static WPEInputMethodContextOHOS* contextForProxy(InputMethod_TextEditorProxy* proxy)
{
    auto it = proxyToContextMap().find(proxy);
    return it != proxyToContextMap().end() ? it->second : nullptr;
}

static InputMethod_TextInputType toInputType(WPEInputPurpose purpose)
{
    switch (purpose) {
    case WPE_INPUT_PURPOSE_DIGITS:
    case WPE_INPUT_PURPOSE_NUMBER:
        return IME_TEXT_INPUT_TYPE_NUMBER;
    case WPE_INPUT_PURPOSE_PHONE:
        return IME_TEXT_INPUT_TYPE_PHONE;
    case WPE_INPUT_PURPOSE_URL:
        return IME_TEXT_INPUT_TYPE_URL;
    case WPE_INPUT_PURPOSE_EMAIL:
        return IME_TEXT_INPUT_TYPE_EMAIL_ADDRESS;
    case WPE_INPUT_PURPOSE_PASSWORD:
        return IME_TEXT_INPUT_TYPE_VISIBLE_PASSWORD;
    case WPE_INPUT_PURPOSE_PIN:
        return IME_TEXT_INPUT_TYPE_NUMBER_PASSWORD;
    default:
        return IME_TEXT_INPUT_TYPE_TEXT;
    }
}

// Number of UTF-16 code units in the first byteLength bytes of a UTF-8 string.
static unsigned utf16UnitsForPrefix(const char* utf8, unsigned byteLength)
{
    if (!utf8 || !byteLength)
        return 0;

    glong units = 0;
    g_autofree gunichar2* u16 = g_utf8_to_utf16(utf8, byteLength, nullptr, &units, nullptr);
    return u16 ? static_cast<unsigned>(units) : 0;
}

// Copies up to `number` UTF-16 units from `utf8` into `out`, from the end when `fromEnd` is set
// (text left of the cursor) or the start otherwise. Returns the number of units written.
static size_t copyUtf16Window(const char* utf8, unsigned byteLength, int32_t number, bool fromEnd, char16_t out[])
{
    if (!utf8 || !byteLength || number <= 0)
        return 0;

    glong units = 0;
    g_autofree gunichar2* u16 = g_utf8_to_utf16(utf8, byteLength, nullptr, &units, nullptr);
    if (!u16 || units <= 0)
        return 0;

    size_t count = std::min(static_cast<size_t>(number), static_cast<size_t>(units));
    const gunichar2* src = fromEnd ? u16 + (units - count) : u16;
    memcpy(out, src, count * sizeof(char16_t));
    return count;
}

static WPEView* viewForProxy(InputMethod_TextEditorProxy* proxy)
{
    auto* self = contextForProxy(proxy);
    return self ? wpe_input_method_context_get_view(WPE_INPUT_METHOD_CONTEXT(self)) : nullptr;
}

// Sends a synthetic key press + release, so keyboard actions WebKit only exposes through key
// handling (Enter, arrow-key caret moves, Ctrl shortcuts) reach the page.
static void dispatchKeyPress(WPEView* view, guint keyval, WPEModifiers modifiers)
{
    for (auto eventType : { WPE_EVENT_KEYBOARD_KEY_DOWN, WPE_EVENT_KEYBOARD_KEY_UP }) {
        WPEEvent* event = wpe_event_keyboard_new(eventType, view, WPE_INPUT_SOURCE_KEYBOARD, 0, modifiers, 0, keyval);
        wpe_view_event(view, event);
        wpe_event_unref(event);
    }
}

// --- Editor-proxy callbacks (called by the input method service) ---

static void ohosTextEditorGetTextConfig(InputMethod_TextEditorProxy* proxy, InputMethod_TextConfig* config)
{
    auto* self = contextForProxy(proxy);
    if (!self)
        return;

    auto purpose = wpe_input_method_context_get_input_purpose(WPE_INPUT_METHOD_CONTEXT(self));
    OH_TextConfig_SetInputType(config, toInputType(purpose));
    OH_TextConfig_SetEnterKeyType(config, IME_ENTER_KEY_UNSPECIFIED);
    OH_TextConfig_SetPreviewTextSupport(config, true);

    if (self->surroundingText) {
        unsigned cursor = utf16UnitsForPrefix(self->surroundingText, self->surroundingCursorIndex);
        unsigned anchor = utf16UnitsForPrefix(self->surroundingText, self->surroundingSelectionIndex);
        OH_TextConfig_SetSelection(config, cursor, anchor);
    }
}

static void ohosTextEditorInsertText(InputMethod_TextEditorProxy* proxy, const char16_t* text, size_t length)
{
    auto* self = contextForProxy(proxy);
    if (!self || !text)
        return;

    g_autofree char* utf8 = g_utf16_to_utf8(reinterpret_cast<const gunichar2*>(text), length, nullptr, nullptr, nullptr);
    if (utf8)
        g_signal_emit_by_name(self, "committed", utf8);
}

static void ohosTextEditorDeleteForward(InputMethod_TextEditorProxy* proxy, int32_t length)
{
    auto* self = contextForProxy(proxy);
    if (!self || length <= 0)
        return;

    // Delete forward from the cursor (offset 0).
    g_signal_emit_by_name(self, "delete-surrounding", 0, static_cast<guint>(length));
}

static void ohosTextEditorDeleteBackward(InputMethod_TextEditorProxy* proxy, int32_t length)
{
    auto* self = contextForProxy(proxy);
    if (!self || length <= 0)
        return;

    // Delete before the cursor (negative offset).
    g_signal_emit_by_name(self, "delete-surrounding", -length, static_cast<guint>(length));
}

static int32_t ohosTextEditorSetPreviewText(InputMethod_TextEditorProxy* proxy, const char16_t text[], size_t length, int32_t start, int32_t end)
{
    auto* self = contextForProxy(proxy);
    if (!self)
        return IME_ERR_OK;

    bool wasEmpty = !self->preedit || !*self->preedit;

    g_clear_pointer(&self->preedit, g_free);
    self->preedit = text ? g_utf16_to_utf8(reinterpret_cast<const gunichar2*>(text), length, nullptr, nullptr, nullptr) : g_strdup("");
    self->preeditCursor = end >= 0 ? end : 0;

    if (wasEmpty && self->preedit && *self->preedit)
        g_signal_emit_by_name(self, "preedit-started");
    g_signal_emit_by_name(self, "preedit-changed");

    return IME_ERR_OK;
}

static void ohosTextEditorFinishTextPreview(InputMethod_TextEditorProxy* proxy)
{
    auto* self = contextForProxy(proxy);
    if (!self)
        return;

    if (!self->preedit || !*self->preedit)
        return;

    g_clear_pointer(&self->preedit, g_free);
    self->preedit = g_strdup("");
    self->preeditCursor = 0;

    g_signal_emit_by_name(self, "preedit-changed");
    g_signal_emit_by_name(self, "preedit-finished");
}

static void ohosTextEditorGetLeftTextOfCursor(InputMethod_TextEditorProxy* proxy, int32_t number, char16_t text[], size_t* length)
{
    auto* self = contextForProxy(proxy);
    if (!self || !length)
        return;

    *length = self->surroundingText
        ? copyUtf16Window(self->surroundingText, self->surroundingCursorIndex, number, true, text)
        : 0;
}

static void ohosTextEditorGetRightTextOfCursor(InputMethod_TextEditorProxy* proxy, int32_t number, char16_t text[], size_t* length)
{
    auto* self = contextForProxy(proxy);
    if (!self || !length)
        return;

    if (!self->surroundingText) {
        *length = 0;
        return;
    }

    const char* right = self->surroundingText + self->surroundingCursorIndex;
    *length = copyUtf16Window(right, strlen(right), number, false, text);
}

static int32_t ohosTextEditorGetTextIndexAtCursor(InputMethod_TextEditorProxy* proxy)
{
    auto* self = contextForProxy(proxy);
    if (!self || !self->surroundingText)
        return 0;

    return static_cast<int32_t>(utf16UnitsForPrefix(self->surroundingText, self->surroundingCursorIndex));
}

static void ohosTextEditorSendKeyboardStatus(InputMethod_TextEditorProxy*, InputMethod_KeyboardStatus)
{
}

static void ohosTextEditorSendEnterKey(InputMethod_TextEditorProxy* proxy, InputMethod_EnterKeyType)
{
    auto* view = viewForProxy(proxy);
    if (!view)
        return;

    // Return triggers the field's default action (submit, newline, ...).
    dispatchKeyPress(view, 0xff0d /* XKB_KEY_Return */, static_cast<WPEModifiers>(0));
}

// No public WPE API sets a selection by text offset, so HandleSetSelection stays a no-op;
// MoveCursor and HandleExtendAction map to the matching key strokes.

static void ohosTextEditorMoveCursor(InputMethod_TextEditorProxy* proxy, InputMethod_Direction direction)
{
    auto* view = viewForProxy(proxy);
    if (!view)
        return;

    guint keyval = 0;
    switch (direction) {
    case IME_DIRECTION_UP:
        keyval = 0xff52; // XKB_KEY_Up
        break;
    case IME_DIRECTION_DOWN:
        keyval = 0xff54; // XKB_KEY_Down
        break;
    case IME_DIRECTION_LEFT:
        keyval = 0xff51; // XKB_KEY_Left
        break;
    case IME_DIRECTION_RIGHT:
        keyval = 0xff53; // XKB_KEY_Right
        break;
    default:
        return;
    }

    dispatchKeyPress(view, keyval, static_cast<WPEModifiers>(0));
}

static void ohosTextEditorHandleSetSelection(InputMethod_TextEditorProxy*, int32_t, int32_t)
{
}

static void ohosTextEditorHandleExtendAction(InputMethod_TextEditorProxy* proxy, InputMethod_ExtendAction action)
{
    auto* view = viewForProxy(proxy);
    if (!view)
        return;

    // Ctrl+A/X/C/V. Cut/copy/paste need a WPEDisplay clipboard backend (none on OHOS yet);
    // select-all works without one.
    guint keyval = 0;
    switch (action) {
    case IME_EXTEND_ACTION_SELECT_ALL:
        keyval = 0x61; // 'a'
        break;
    case IME_EXTEND_ACTION_CUT:
        keyval = 0x78; // 'x'
        break;
    case IME_EXTEND_ACTION_COPY:
        keyval = 0x63; // 'c'
        break;
    case IME_EXTEND_ACTION_PASTE:
        keyval = 0x76; // 'v'
        break;
    default:
        return;
    }

    dispatchKeyPress(view, keyval, WPE_MODIFIER_KEYBOARD_CONTROL);
}

static int32_t ohosTextEditorReceivePrivateCommand(InputMethod_TextEditorProxy*, InputMethod_PrivateCommand* [], size_t)
{
    return IME_ERR_OK;
}

static void ohosTextEditorConfigure(InputMethod_TextEditorProxy* proxy)
{
    OH_TextEditorProxy_SetGetTextConfigFunc(proxy, ohosTextEditorGetTextConfig);
    OH_TextEditorProxy_SetInsertTextFunc(proxy, ohosTextEditorInsertText);
    OH_TextEditorProxy_SetDeleteForwardFunc(proxy, ohosTextEditorDeleteForward);
    OH_TextEditorProxy_SetDeleteBackwardFunc(proxy, ohosTextEditorDeleteBackward);
    OH_TextEditorProxy_SetSendKeyboardStatusFunc(proxy, ohosTextEditorSendKeyboardStatus);
    OH_TextEditorProxy_SetSendEnterKeyFunc(proxy, ohosTextEditorSendEnterKey);
    OH_TextEditorProxy_SetMoveCursorFunc(proxy, ohosTextEditorMoveCursor);
    OH_TextEditorProxy_SetHandleSetSelectionFunc(proxy, ohosTextEditorHandleSetSelection);
    OH_TextEditorProxy_SetHandleExtendActionFunc(proxy, ohosTextEditorHandleExtendAction);
    OH_TextEditorProxy_SetReceivePrivateCommandFunc(proxy, ohosTextEditorReceivePrivateCommand);
    OH_TextEditorProxy_SetGetLeftTextOfCursorFunc(proxy, ohosTextEditorGetLeftTextOfCursor);
    OH_TextEditorProxy_SetGetRightTextOfCursorFunc(proxy, ohosTextEditorGetRightTextOfCursor);
    OH_TextEditorProxy_SetGetTextIndexAtCursorFunc(proxy, ohosTextEditorGetTextIndexAtCursor);
    OH_TextEditorProxy_SetSetPreviewTextFunc(proxy, ohosTextEditorSetPreviewText);
    OH_TextEditorProxy_SetFinishTextPreviewFunc(proxy, ohosTextEditorFinishTextPreview);

    // Deliver callbacks on the main thread (WebKit's UIProcess thread).
    OH_TextEditorProxy_SetCallbackInMainThread(proxy, true);
}

// --- WPEInputMethodContext vfuncs ---

static void wpeInputMethodContextOHOSGetPreeditString(WPEInputMethodContext* context, char** text, GList** underlines, guint* cursorOffset)
{
    auto* self = WPE_INPUT_METHOD_CONTEXT_OHOS(context);

    if (text)
        *text = g_strdup(self->preedit ? self->preedit : "");

    if (underlines) {
        *underlines = nullptr;
        if (self->preedit && *self->preedit) {
            long chars = g_utf8_strlen(self->preedit, -1);
            *underlines = g_list_prepend(*underlines, wpe_input_method_underline_new(0, chars));
        }
    }

    if (cursorOffset)
        *cursorOffset = self->preeditCursor;
}

static gboolean wpeInputMethodContextOHOSFilterKeyEvent(WPEInputMethodContext*, WPEEvent*)
{
    // The keyboard delivers text through the editor-proxy callbacks, not key events.
    return FALSE;
}

static void wpeInputMethodContextOHOSFocusIn(WPEInputMethodContext* context)
{
    auto* self = WPE_INPUT_METHOD_CONTEXT_OHOS(context);
    if (self->imProxy)
        return;

    if (!self->editorProxy) {
        self->editorProxy = OH_TextEditorProxy_Create();
        if (!self->editorProxy) {
            LOGE("WPEInputMethodContextOHOS: failed to create text editor proxy");
            return;
        }
        ohosTextEditorConfigure(self->editorProxy);
        proxyToContextMap().emplace(self->editorProxy, self);
    }

    InputMethod_AttachOptions* options = OH_AttachOptions_Create(true);
    InputMethod_ErrorCode result = OH_InputMethodController_Attach(self->editorProxy, options, &self->imProxy);
    OH_AttachOptions_Destroy(options);

    if (result != IME_ERR_OK) {
        LOGE("WPEInputMethodContextOHOS: attach failed (%{public}d)", result);
        self->imProxy = nullptr;
        return;
    }

    if (OH_InputMethodProxy_ShowKeyboard(self->imProxy) != IME_ERR_OK)
        LOGE("WPEInputMethodContextOHOS: failed to show keyboard");
}

static void wpeInputMethodContextOHOSFocusOut(WPEInputMethodContext* context)
{
    auto* self = WPE_INPUT_METHOD_CONTEXT_OHOS(context);
    if (!self->imProxy)
        return;

    OH_InputMethodController_Detach(self->imProxy);
    self->imProxy = nullptr;
}

static void wpeInputMethodContextOHOSSetCursorArea(WPEInputMethodContext* context, int x, int y, int width, int height)
{
    auto* self = WPE_INPUT_METHOD_CONTEXT_OHOS(context);
    self->cursorRect = { x, y, width, height };

    if (!self->imProxy)
        return;

    InputMethod_CursorInfo* cursorInfo = OH_CursorInfo_Create(x, y, width, height);
    OH_InputMethodProxy_NotifyCursorUpdate(self->imProxy, cursorInfo);
    OH_CursorInfo_Destroy(cursorInfo);
}

static void wpeInputMethodContextOHOSSetSurrounding(WPEInputMethodContext* context, const char* text, guint length, guint cursorIndex, guint selectionIndex)
{
    auto* self = WPE_INPUT_METHOD_CONTEXT_OHOS(context);

    g_clear_pointer(&self->surroundingText, g_free);
    self->surroundingText = g_strndup(text ? text : "", length);
    self->surroundingCursorIndex = cursorIndex;
    self->surroundingSelectionIndex = selectionIndex;

    if (!self->imProxy)
        return;

    glong units = 0;
    g_autofree gunichar2* u16 = g_utf8_to_utf16(self->surroundingText, length, nullptr, &units, nullptr);
    if (!u16)
        return;

    int start = utf16UnitsForPrefix(self->surroundingText, cursorIndex);
    int end = utf16UnitsForPrefix(self->surroundingText, selectionIndex);
    OH_InputMethodProxy_NotifySelectionChange(self->imProxy, reinterpret_cast<char16_t*>(u16), units, start, end);
}

static void wpeInputMethodContextOHOSReset(WPEInputMethodContext* context)
{
    auto* self = WPE_INPUT_METHOD_CONTEXT_OHOS(context);
    if (!self->preedit || !*self->preedit)
        return;

    g_clear_pointer(&self->preedit, g_free);
    self->preedit = g_strdup("");
    self->preeditCursor = 0;

    g_signal_emit_by_name(self, "preedit-changed");
    g_signal_emit_by_name(self, "preedit-finished");
}

static void wpeInputMethodContextOHOSDispose(GObject* object)
{
    auto* self = WPE_INPUT_METHOD_CONTEXT_OHOS(object);

    if (self->imProxy) {
        OH_InputMethodController_Detach(self->imProxy);
        self->imProxy = nullptr;
    }

    if (self->editorProxy) {
        proxyToContextMap().erase(self->editorProxy);
        OH_TextEditorProxy_Destroy(self->editorProxy);
        self->editorProxy = nullptr;
    }

    g_clear_pointer(&self->preedit, g_free);
    g_clear_pointer(&self->surroundingText, g_free);

    G_OBJECT_CLASS(wpe_input_method_context_ohos_parent_class)->dispose(object);
}

static void wpe_input_method_context_ohos_class_init(WPEInputMethodContextOHOSClass* klass)
{
    G_OBJECT_CLASS(klass)->dispose = wpeInputMethodContextOHOSDispose;

    WPEInputMethodContextClass* imClass = WPE_INPUT_METHOD_CONTEXT_CLASS(klass);
    imClass->get_preedit_string = wpeInputMethodContextOHOSGetPreeditString;
    imClass->filter_key_event = wpeInputMethodContextOHOSFilterKeyEvent;
    imClass->focus_in = wpeInputMethodContextOHOSFocusIn;
    imClass->focus_out = wpeInputMethodContextOHOSFocusOut;
    imClass->set_cursor_area = wpeInputMethodContextOHOSSetCursorArea;
    imClass->set_surrounding = wpeInputMethodContextOHOSSetSurrounding;
    imClass->reset = wpeInputMethodContextOHOSReset;
}

static void wpe_input_method_context_ohos_init(WPEInputMethodContextOHOS*)
{
}

WPEInputMethodContext* wpe_input_method_context_ohos_new(WPEView* view)
{
    return WPE_INPUT_METHOD_CONTEXT(g_object_new(WPE_TYPE_INPUT_METHOD_CONTEXT_OHOS, "view", view, nullptr));
}
