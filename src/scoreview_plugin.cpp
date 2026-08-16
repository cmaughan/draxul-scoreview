#include <draxul/nanovg_pass.h>
#include <draxul/plugin_adapter.h>
#include <draxul/plugin_api.h>
#include <draxul/plugin_gpu_imgui.h>
#include <draxul/scoreview/score_runtime.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace
{

// The adapter shell (result factories, config parse, action registrar, kApi
// assembly) comes from draxul/plugin_adapter.h — header-only, so it is also
// available to the standalone extraction build through the installed SDK.
// kActiveDelayNs is the shared ~60 Hz frame delay.
using draxul::plugin_support::kFrameDelayNs;
using draxul::plugin_support::render_result;
using draxul::plugin_support::tick_result;

constexpr const char* kPluginId = "dev.draxul.scoreview";
constexpr uint64_t kActiveDelayNs = kFrameDelayNs;
constexpr uint64_t kWorkerDelayNs = 100'000'000;

class RuntimeCallbacks final
    : public draxul::scoreview::ScoreRuntimeCallbacks
{
public:
    void request_frame() override
    {
        if (!host)
            return;
        if (host->request_tick)
            host->request_tick(host->host_context);
        if (visible && host->request_redraw)
            host->request_redraw(host->host_context);
    }
    void notify_presentation_changed() override
    {
        if (host && host->notify_presentation_changed)
            host->notify_presentation_changed(host->host_context);
    }
    const DraxulPluginHostApiV2* host = nullptr;
    bool visible = true;
};

struct Instance;

class FrameSink final : public draxul::scoreview::ScoreFrameSink
{
public:
    FrameSink(Instance& instance, const DraxulPluginVulkanFrameV2* vulkan,
        const DraxulPluginMetalFrameV2* metal)
        : instance_(instance), vulkan_(vulkan), metal_(metal)
    {
    }
    void record_canvas(draxul::INanoVGPass& pass,
        int, int, int, int) override;
    void render_overlay(void* draw_data, void* context) override;
    void finish() override {}
    bool ok() const { return ok_; }
private:
    Instance& instance_;
    const DraxulPluginVulkanFrameV2* vulkan_ = nullptr;
    const DraxulPluginMetalFrameV2* metal_ = nullptr;
    bool ok_ = true;
};

struct Instance
{
    const DraxulPluginHostApiV2* host = nullptr;
    DraxulPluginPathServiceV2 paths{};
    draxul::plugin_support::UiStyleClient ui_style;
    DraxulPluginViewportV2 viewport{};
    RuntimeCallbacks callbacks;
    std::unique_ptr<draxul::plugin_support::GpuImGuiHost> overlay;
    std::unique_ptr<draxul::scoreview::ScoreRuntime> runtime;
    std::filesystem::path directory;
    std::string status;
    bool visible = true;
    bool focused = false;
    bool background_playback = false;
    bool quiesced = false;
};

void FrameSink::render_overlay(void* draw_data, void* context)
{
    if (instance_.overlay)
        ok_ = instance_.overlay->render_imgui_draw_data(
            static_cast<ImDrawData*>(draw_data),
            static_cast<ImGuiContext*>(context)) && ok_;
}

void FrameSink::record_canvas(draxul::INanoVGPass& pass,
    int, int, int, int)
{
    if (vulkan_)
        ok_ = pass.render_vulkan(*vulkan_) && ok_;
    else if (metal_)
        ok_ = pass.render_metal(*metal_) && ok_;
}

// Raw path-service reader (guarded variant: required==0 can never underflow
// the resize). This stays hand-rolled rather than using HostServices because
// the standalone extraction build links only the installed SDK.
std::string service_path(DraxulPluginPathServiceV2& service,
    uint32_t kind)
{
    size_t required = 0;
    if (!service.get_path || !service.get_path(service.service_context,
            kind, nullptr, &required) || required == 0)
        return {};
    std::string value(required, '\0');
    if (!service.get_path(service.service_context, kind,
            value.data(), &required))
        return {};
    value.resize(required > 0 ? required - 1 : 0);
    return value;
}

void synchronize_ui_style(Instance& instance)
{
    draxul::plugin_support::synchronize_ui_style(
        instance.ui_style, instance.runtime.get());
}

void* create_instance(const DraxulPluginCreateInfoV2* info)
{
    if (!info || !info->host)
        return nullptr;
    auto instance = std::make_unique<Instance>();
    instance->host = info->host;
    instance->callbacks.host = info->host;
    instance->viewport = info->initial_viewport;
    instance->directory = info->plugin_directory_utf8
        ? std::filesystem::u8path(info->plugin_directory_utf8)
        : std::filesystem::path{};

    instance->paths.struct_size = sizeof(instance->paths);
    instance->paths.service_version = DRAXUL_PLUGIN_PATH_SERVICE_VERSION;
    if (info->host->query_service)
    {
        info->host->query_service(info->host->host_context,
            DRAXUL_PLUGIN_PATH_SERVICE_ID,
            std::strlen(DRAXUL_PLUGIN_PATH_SERVICE_ID),
            DRAXUL_PLUGIN_PATH_SERVICE_VERSION,
            &instance->paths, sizeof(instance->paths));
        instance->ui_style.discover(*info->host);
    }
    instance->overlay = draxul::plugin_support::create_gpu_imgui_host();

    std::string source;
    std::string mode = "paged";
    const auto config = draxul::plugin_support::parse_config_json(*info);
    if (!config)
        return nullptr;
    try
    {
        source = config->value("source", std::string{});
        mode = config->value("mode", mode);
        instance->background_playback
            = config->value("background_playback", false);
    }
    catch (...)
    {
        return nullptr;
    }

    draxul::HostContext context;
    context.launch_options.source_path = source;
    context.launch_options.command = mode;
    context.initial_viewport.pixel_pos = {
        info->initial_viewport.x, info->initial_viewport.y };
    context.initial_viewport.pixel_size = {
        info->initial_viewport.width, info->initial_viewport.height };
    context.initial_viewport.pixel_scale = info->initial_viewport.pixel_scale;
    draxul::scoreview::ScoreRuntimePaths paths;
    paths.verovio_data = instance->directory / "verovio-data";
    paths.soundfonts = instance->directory / "soundfonts";
    const std::string data = service_path(instance->paths,
        DRAXUL_PLUGIN_PATH_DATA);
    if (!data.empty())
        paths.progress = std::filesystem::u8path(data) / "progress";
    draxul::set_nanovg_asset_root(instance->directory);
    instance->runtime = std::make_unique<draxul::scoreview::ScoreRuntime>();
    if (!instance->runtime->initialize(context, instance->callbacks,
            std::move(paths)))
    {
        const std::string error = "ScoreView initialization failed: "
            + instance->runtime->init_error();
        if (info->host->log)
            info->host->log(info->host->host_context,
                DRAXUL_PLUGIN_LOG_ERROR, error.c_str(), error.size());
        return nullptr;
    }
    if (instance->overlay)
        instance->runtime->attach_imgui_host(*instance->overlay);
    synchronize_ui_style(*instance);
    return instance.release();
}

void quiesce_instance(void* opaque)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance || instance->quiesced)
        return;
    instance->runtime->quiesce();
    instance->quiesced = true;
}

void destroy_instance(void* opaque)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance)
        return;
    instance->runtime->shutdown();
    delete instance;
}

void set_viewport(void* opaque, const DraxulPluginViewportV2* viewport)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance || !viewport)
        return;
    instance->viewport = *viewport;
    draxul::HostViewport value;
    value.pixel_pos = { viewport->x, viewport->y };
    value.pixel_size = { viewport->width, viewport->height };
    value.pixel_scale = viewport->pixel_scale;
    instance->runtime->set_viewport(value);
}

void set_visible(void* opaque, int32_t visible)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance)
        return;
    instance->visible = visible != 0;
    instance->callbacks.visible = instance->visible;
    instance->runtime->set_presentation_visible(instance->visible,
        instance->background_playback);
    if (instance->host->request_tick)
        instance->host->request_tick(instance->host->host_context);
    if (instance->visible && instance->host->request_redraw)
        instance->host->request_redraw(instance->host->host_context);
}

void set_focused(void* opaque, int32_t focused)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (instance)
        instance->focused = focused != 0;
}

int32_t handle_input(void* opaque, const DraxulPluginInputEventV2* event)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance || !event)
        return 0;
    const glm::ivec2 position{
        event->x + instance->viewport.x,
        event->y + instance->viewport.y };
    switch (event->kind)
    {
    case DRAXUL_PLUGIN_INPUT_KEY:
        instance->runtime->on_key({ static_cast<int>(event->physical_key),
            event->logical_key,
            static_cast<draxul::ModifierFlags>(event->modifiers),
            event->pressed != 0 });
        return 1;
    case DRAXUL_PLUGIN_INPUT_TEXT:
        instance->runtime->on_text_input({ std::string(
            event->text_utf8 ? event->text_utf8 : "",
            event->text_utf8 ? event->text_length : 0) });
        return 1;
    case DRAXUL_PLUGIN_INPUT_WHEEL:
        instance->runtime->on_mouse_wheel({
            { event->delta_x, event->delta_y },
            static_cast<draxul::ModifierFlags>(event->modifiers), position });
        return 1;
    case DRAXUL_PLUGIN_INPUT_POINTER_BUTTON:
        instance->runtime->on_mouse_button({ event->button,
            event->pressed != 0,
            static_cast<draxul::ModifierFlags>(event->modifiers), position,
            event->clicks });
        return 1;
    case DRAXUL_PLUGIN_INPUT_POINTER_MOVE:
        instance->runtime->on_mouse_move({
            static_cast<draxul::ModifierFlags>(event->modifiers), position,
            { event->delta_x, event->delta_y }, event->buttons });
        return 1;
    default:
        return 0;
    }
}

DraxulPluginTickResultV2 tick(void* opaque,
    const DraxulPluginTickInfoV2* info)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance || !info || instance->quiesced)
        return tick_result(instance != nullptr, DRAXUL_PLUGIN_NO_DEADLINE);
    synchronize_ui_style(*instance);
    instance->runtime->pump();
    const auto deadline = instance->runtime->next_deadline();
    if (!instance->visible || !info->visible)
        return tick_result(true, deadline
                ? (instance->background_playback
                    ? kActiveDelayNs : kWorkerDelayNs)
                                          : DRAXUL_PLUGIN_NO_DEADLINE);
    return tick_result(true, deadline ? kActiveDelayNs
                                      : DRAXUL_PLUGIN_NO_DEADLINE,
        deadline.has_value());
}

DraxulPluginRenderResultV2 render_vulkan(void* opaque,
    const DraxulPluginVulkanFrameV2* frame)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance || !instance->visible || instance->quiesced)
        return render_result(true);
    if (!frame || !frame->command_buffer || !frame->device
        || !frame->continuation_render_pass
        || !frame->continuation_framebuffer)
        return render_result(false, "ScoreView received an incomplete Vulkan frame");
    synchronize_ui_style(*instance);
    if (instance->overlay)
        instance->overlay->set_vulkan_frame(frame);
    FrameSink sink(*instance, frame, nullptr);
    instance->runtime->draw(sink);
    return render_result(sink.ok(),
        sink.ok() ? nullptr : "ScoreView Vulkan rendering failed");
}

DraxulPluginRenderResultV2 render_metal(void* opaque,
    const DraxulPluginMetalFrameV2* frame)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance || !instance->visible || instance->quiesced)
        return render_result(true);
    if (!frame || !frame->command_buffer || !frame->device
        || !frame->continuation_render_pass_descriptor)
        return render_result(false, "ScoreView received an incomplete Metal frame");
    synchronize_ui_style(*instance);
    if (instance->overlay)
        instance->overlay->set_metal_frame(frame);
    FrameSink sink(*instance, nullptr, frame);
    instance->runtime->draw(sink);
    return render_result(sink.ok(),
        sink.ok() ? nullptr : "ScoreView Metal rendering failed");
}

int32_t get_state(void* opaque, DraxulPluginPresentationStateV2* state)
{
    auto* instance = static_cast<Instance*>(opaque);
    if (!instance || !state || state->struct_size < sizeof(*state))
        return 0;
    instance->status = instance->runtime->status_text();
    *state = {};
    state->struct_size = sizeof(*state);
    state->display_name = { "ScoreView", 9 };
    state->status_text = { instance->status.data(), instance->status.size() };
    const auto background = instance->runtime->default_background();
    state->background_red = background.r;
    state->background_green = background.g;
    state->background_blue = background.b;
    state->background_alpha = background.a;
    state->content_ready = instance->runtime->runtime_state().content_ready;
    const auto hint = instance->runtime->print_hint();
    state->print_hint = { hint.content_pos.x, hint.content_pos.y,
        hint.content_size.x, hint.content_size.y, hint.paper_white ? 1 : 0 };
    return 1;
}

int32_t dispatch_action(void* opaque, const char* action, size_t length)
{
    auto* instance = static_cast<Instance*>(opaque);
    return instance && action && instance->runtime->dispatch_action(
        std::string_view(action, length)) ? 1 : 0;
}

constexpr draxul::plugin_support::AdapterAction kActions[] = {
    { "font_increase", "Zoom In" },
    { "font_decrease", "Zoom Out" },
    { "font_reset", "Reset Zoom" },
};

using Presentation = draxul::plugin_support::PresentationAdapter<kActions,
    &get_state, &dispatch_action>;

const DraxulPluginApiV2 kApi = draxul::plugin_support::make_plugin_api(
    { kPluginId, "ScoreView", "0.1.0",
        DRAXUL_PLUGIN_BACKEND_VULKAN | DRAXUL_PLUGIN_BACKEND_METAL },
    {
        .create_instance = &create_instance,
        .quiesce_instance = &quiesce_instance,
        .destroy_instance = &destroy_instance,
        .set_viewport = &set_viewport,
        .set_visible = &set_visible,
        .set_focused = &set_focused,
        .handle_input = &handle_input,
        .tick = &tick,
        .render_vulkan = &render_vulkan,
        .render_metal = &render_metal,
        .query_extension = &Presentation::query_extension,
    });

} // namespace

extern "C" DRAXUL_PLUGIN_EXPORT const DraxulPluginApiV2*
draxul_plugin_query_v2(uint32_t requested_abi)
{
    return requested_abi == DRAXUL_PLUGIN_ABI_VERSION ? &kApi : nullptr;
}
