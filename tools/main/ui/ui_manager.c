/*
 * Layer 1 - UI manager framework.
 *
 * A pure navigation engine: it owns the view stack, the re-entrancy guard
 * and the per view creation state. Everything view specific arrives through
 * ui_view_t descriptors published by ui_manager_register().
 */

#include "ui_manager.h"

#include "esp_log.h"

static const char *TAG = "ui_mgr";

#define UI_STACK_MAX 16

#define UI_LOG(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)

/* Runtime state of one view. The descriptor itself stays immutable. */
typedef struct {
    const ui_view_t *view;  /* NULL until the view is registered */
    bool created;           /* init() ran, deinit() did not yet   */
} ui_view_state_t;

static ui_view_state_t g_state[VIEW_MAX];
static view_id_t g_stack[UI_STACK_MAX];
static int g_top = -1;                 /* index of the active view, -1 = empty */
static bool g_initialized = false;
static bool g_transitioning = false;   /* re-entrancy guard */

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static bool id_valid(view_id_t id)
{
    return id >= 0 && id < VIEW_MAX;
}

static const ui_view_t *view_of(view_id_t id)
{
    return id_valid(id) ? g_state[id].view : NULL;
}

static const char *name_of(view_id_t id)
{
    const ui_view_t *view = view_of(id);

    return (view != NULL && view->name != NULL) ? view->name : "?";
}

/* Run a single lifecycle phase when the view implements it. */
static void lifecycle(const ui_view_t *view, ui_view_cb_t phase, const char *what)
{
    if (view == NULL || phase == NULL) {
        return;
    }

    UI_LOG("%s: %s", name_of(view->id), what);
    phase();
}

/* Reject nested transitions (a callback started another navigation). */
static bool transition_allowed(const char *op)
{
    if (!g_transitioning) {
        return true;
    }

    UI_LOG("%s rejected: transition in progress", op);
    return false;
}

/* Validate an id for push/switch: in range and registered. */
static bool target_valid(const char *op, view_id_t id)
{
    if (!id_valid(id)) {
        UI_LOG("%s rejected: invalid id=%d", op, (int)id);
        return false;
    }

    if (g_state[id].view == NULL) {
        UI_LOG("%s rejected: view %d not registered", op, (int)id);
        return false;
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* View stack                                                          */
/* ------------------------------------------------------------------ */

static view_id_t stack_top(void)
{
    if (g_top < 0) {
        return VIEW_NONE;
    }

    return g_stack[g_top];
}

static bool stack_push(view_id_t id)
{
    if (g_top >= UI_STACK_MAX - 1) {
        UI_LOG("stack overflow, id=%d", (int)id);
        return false;
    }

    g_stack[++g_top] = id;
    return true;
}

static view_id_t stack_pop(void)
{
    if (g_top < 0) {
        return VIEW_NONE;
    }

    return g_stack[g_top--];
}

/* ------------------------------------------------------------------ */
/* View creation state                                                 */
/* ------------------------------------------------------------------ */

static bool ensure_created(const ui_view_t *view)
{
    if (view == NULL) {
        return false;
    }

    if (!g_state[view->id].created) {
        lifecycle(view, view->init, "init");
        g_state[view->id].created = true;
    }

    return true;
}

static void destroy_view(view_id_t id)
{
    ui_view_state_t *state = id_valid(id) ? &g_state[id] : NULL;

    /* A view that was never created has nothing to close or free. */
    if (state == NULL || !state->created) {
        return;
    }

    lifecycle(state->view, state->view->close, "close");
    lifecycle(state->view, state->view->deinit, "deinit");
    state->created = false;
}

/* Drop the whole stack: used by ui_switch_view() and ui_manager_deinit(). */
static void stack_clear(void)
{
    view_id_t id;

    while ((id = stack_pop()) != VIEW_NONE) {
        destroy_view(id);
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

bool ui_manager_register(const ui_view_t *view)
{
    if (view == NULL || !id_valid(view->id)) {
        UI_LOG("register rejected: invalid view");
        return false;
    }

    if (g_state[view->id].view != NULL) {
        UI_LOG("register rejected: %s already registered",
               name_of(view->id));
        return false;
    }

    g_state[view->id].view = view;
    UI_LOG("registered %s", name_of(view->id));
    return true;
}

void ui_push_view(view_id_t id)
{
    if (!target_valid("push", id)) {
        return;
    }

    if (!transition_allowed("push")) {
        return;
    }

    if (stack_top() == id) {
        UI_LOG("push rejected: %s already active", name_of(id));
        return;
    }

    if (g_top >= UI_STACK_MAX - 1) {
        UI_LOG("push rejected: stack full");
        return;
    }

    g_transitioning = true;

    const ui_view_t *current = view_of(stack_top());
    const ui_view_t *next = view_of(id);

    if (current != NULL) {
        lifecycle(current, current->pause, "pause");
    }

    ensure_created(next);

    /* Push before open() so lifecycle callbacks observe the active view. */
    stack_push(id);

    lifecycle(next, next->open, "open");

    UI_LOG("active=%s depth=%d", name_of(id), g_top + 1);
    g_transitioning = false;
}

void ui_pop_view(void)
{
    /*
     * The root view cannot be popped: the stack always keeps at least
     * one view, so ui_current_view() never returns VIEW_NONE.
     */
    if (g_top <= 0) {
        UI_LOG("pop rejected: root view cannot be popped");
        return;
    }

    if (!transition_allowed("pop")) {
        return;
    }

    g_transitioning = true;

    view_id_t leaving = stack_pop();

    UI_LOG("--- POP: remove %s, back to %s ---",
           name_of(leaving), name_of(stack_top()));

    destroy_view(leaving);

    const ui_view_t *back = view_of(stack_top());

    if (back != NULL) {
        lifecycle(back, back->resume, "resume");
    }

    g_transitioning = false;
}

void ui_switch_view(view_id_t id)
{
    if (!target_valid("switch", id)) {
        return;
    }

    if (!transition_allowed("switch")) {
        return;
    }

    g_transitioning = true;
    /* Clear directly: ui_pop_view() would protect the root view. */
    stack_clear();
    g_transitioning = false;

    ui_push_view(id);
}

bool ui_manager_init(void)
{
    if (g_initialized) {
        UI_LOG("UI Manager already initialized");
        return true;
    }

    /* The Win11 demo bootstraps straight into the desktop view. */
    return ui_manager_init_with(VIEW_WIN_DESKTOP);
}

bool ui_manager_init_with(view_id_t root_id)
{
    if (g_initialized) {
        UI_LOG("UI Manager already initialized");
        return true;
    }

    UI_LOG("UI Manager init (root=%s)", name_of(root_id));
    g_top = -1;

    ui_push_view(root_id);

    g_initialized = (g_top >= 0);

    if (!g_initialized) {
        UI_LOG("UI Manager init failed: call ui_views_register_all() first");
    }

    return g_initialized;
}

void ui_manager_deinit(void)
{
    if (!g_initialized) {
        return;
    }

    if (!transition_allowed("deinit")) {
        return;
    }

    g_transitioning = true;
    stack_clear();
    g_initialized = false;
    g_transitioning = false;

    UI_LOG("UI Manager deinitialized");
}

view_id_t ui_current_view(void)
{
    return stack_top();
}
