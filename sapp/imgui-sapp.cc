//------------------------------------------------------------------------------
//  imgui-sapp.c
//------------------------------------------------------------------------------
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_time.h"
#include "imgui.h"

const int MaxVertices = (1<<16);
const int MaxIndices = MaxVertices * 3;

uint64_t last_time = 0;
bool show_test_window = true;
bool show_another_window = false;

sg_draw_state draw_state = { };
sg_pass_action pass_action = { };
ImDrawVert vertices[MaxVertices];
uint16_t indices[MaxIndices];
bool btn_down[SAPP_MAX_MOUSE_BUTTONS] = { };
bool btn_up[SAPP_MAX_MOUSE_BUTTONS] = { };

typedef struct {
    ImVec2 disp_size;
} vs_params_t;

void imgui_draw_cb(ImDrawData*);

extern const char* vs_src;
extern const char* fs_src;

void init() {
    // setup sokol-gfx and sokol-time
    sg_desc desc = { };
    desc.gl_force_gles2 = sapp_gles2_fallback(),
    desc.mtl_device = sapp_metal_get_device(),
    desc.mtl_renderpass_descriptor_cb = sapp_metal_get_renderpass_descriptor,
    desc.mtl_drawable_cb = sapp_metal_get_drawable,
    sg_setup(&desc);
    stm_setup();

    // setup Dear Imgui 
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.RenderDrawListsFn = imgui_draw_cb;
    io.Fonts->AddFontDefault();
    /*
    io.KeyMap[ImGuiKey_Tab] = VK_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = VK_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = VK_UP;
    io.KeyMap[ImGuiKey_DownArrow] = VK_DOWN;
    io.KeyMap[ImGuiKey_Home] = VK_HOME;
    io.KeyMap[ImGuiKey_End] = VK_END;
    io.KeyMap[ImGuiKey_Delete] = VK_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = VK_BACK;
    io.KeyMap[ImGuiKey_Enter] = VK_RETURN;
    io.KeyMap[ImGuiKey_Escape] = VK_ESCAPE;
    io.KeyMap[ImGuiKey_A] = 'A';
    io.KeyMap[ImGuiKey_C] = 'C';
    io.KeyMap[ImGuiKey_V] = 'V';
    io.KeyMap[ImGuiKey_X] = 'X';
    io.KeyMap[ImGuiKey_Y] = 'Y';
    io.KeyMap[ImGuiKey_Z] = 'Z';
    */

    // dynamic vertex- and index-buffers for imgui-generated geometry
    sg_buffer_desc vbuf_desc = { };
    vbuf_desc.usage = SG_USAGE_STREAM;
    vbuf_desc.size = sizeof(vertices);
    draw_state.vertex_buffers[0] = sg_make_buffer(&vbuf_desc);

    sg_buffer_desc ibuf_desc = { };
    ibuf_desc.type = SG_BUFFERTYPE_INDEXBUFFER;
    ibuf_desc.usage = SG_USAGE_STREAM;
    ibuf_desc.size = sizeof(indices);
    draw_state.index_buffer = sg_make_buffer(&ibuf_desc);

    // font texture for imgui's default font
    unsigned char* font_pixels;
    int font_width, font_height;
    io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
    sg_image_desc img_desc = { };
    img_desc.width = font_width;
    img_desc.height = font_height;
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    img_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    img_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    img_desc.content.subimage[0][0].ptr = font_pixels;
    img_desc.content.subimage[0][0].size = font_width * font_height * 4;
    draw_state.fs_images[0] = sg_make_image(&img_desc);

    // shader object for imgui rendering
    sg_shader_desc shd_desc = { };
    auto& ub = shd_desc.vs.uniform_blocks[0];
    ub.size = sizeof(vs_params_t);
    ub.uniforms[0].name = "disp_size";
    ub.uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
    shd_desc.fs.images[0].name = "tex";
    shd_desc.fs.images[0].type = SG_IMAGETYPE_2D;
    shd_desc.vs.source = vs_src;
    shd_desc.fs.source = fs_src;
    sg_shader shd = sg_make_shader(&shd_desc);

    // pipeline object for imgui rendering
    sg_pipeline_desc pip_desc = { };
    pip_desc.layout.buffers[0].stride = sizeof(ImDrawVert);
    auto& attrs = pip_desc.layout.attrs;
    attrs[0].name="position"; attrs[0].sem_name="POSITION"; attrs[0].offset=offsetof(ImDrawVert, pos); attrs[0].format=SG_VERTEXFORMAT_FLOAT2;
    attrs[1].name="texcoord0"; attrs[1].sem_name="TEXCOORD"; attrs[1].offset=offsetof(ImDrawVert, uv); attrs[1].format=SG_VERTEXFORMAT_FLOAT2;
    attrs[2].name="color0"; attrs[2].sem_name="COLOR"; attrs[2].offset=offsetof(ImDrawVert, col); attrs[2].format=SG_VERTEXFORMAT_UBYTE4N;
    pip_desc.shader = shd;
    pip_desc.index_type = SG_INDEXTYPE_UINT16;
    pip_desc.blend.enabled = true;
    pip_desc.blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pip_desc.blend.color_write_mask = SG_COLORMASK_RGB;
    draw_state.pipeline = sg_make_pipeline(&pip_desc);

    // initial clear color
    pass_action.colors[0].action = SG_ACTION_CLEAR;
    pass_action.colors[0].val[0] = 0.0f;
    pass_action.colors[0].val[1] = 0.5f;
    pass_action.colors[0].val[2] = 0.7f;
    pass_action.colors[0].val[3] = 1.0f;
}

void frame() {
    const int cur_width = sapp_width();
    const int cur_height = sapp_height();

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(cur_width), float(cur_height));
    io.DeltaTime = (float) stm_sec(stm_laptime(&last_time));
    for (int i = 0; i < SAPP_MAX_MOUSE_BUTTONS; i++) {
        if (io.MouseDown[i]) {
            if (btn_up[i]) {
                io.MouseDown[i] = false;
                btn_up[i] = false;
            }
        }
        else {
            if (btn_down[i]) {
                io.MouseDown[i] = true;
                btn_down[i] = false;
            }
        }
    }
    ImGui::NewFrame();

    // 1. Show a simple window
    // Tip: if we don't call ImGui::Begin()/ImGui::End() the widgets appears in a window automatically called "Debug"
    static float f = 0.0f;
    ImGui::Text("Hello, world!");
    ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
    ImGui::ColorEdit3("clear color", &pass_action.colors[0].val[0]);
    if (ImGui::Button("Test Window")) show_test_window ^= 1;
    if (ImGui::Button("Another Window")) show_another_window ^= 1;
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

    // 2. Show another simple window, this time using an explicit Begin/End pair
    if (show_another_window) {
        ImGui::SetNextWindowSize(ImVec2(200,100), ImGuiSetCond_FirstUseEver);
        ImGui::Begin("Another Window", &show_another_window);
        ImGui::Text("Hello");
        ImGui::End();
    }

    // 3. Show the ImGui test window. Most of the sample code is in ImGui::ShowTestWindow()
    if (show_test_window) {
        ImGui::SetNextWindowPos(ImVec2(460, 20), ImGuiSetCond_FirstUseEver);
        ImGui::ShowTestWindow();
    }

    // the sokol_gfx draw pass
    sg_begin_default_pass(&pass_action, cur_width, cur_height);
    ImGui::Render();
    sg_end_pass();
    sg_commit();
}

void cleanup() {
    sg_shutdown();
}

void input(const sapp_event* event) {
    auto& io = ImGui::GetIO();
    switch (event->type) {
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            io.MousePos.x = event->mouse_x;
            io.MousePos.y = event->mouse_y;
            btn_down[event->mouse_button] = true;
            break;
        case SAPP_EVENTTYPE_MOUSE_UP:
            io.MousePos.x = event->mouse_x;
            io.MousePos.y = event->mouse_y;
            btn_up[event->mouse_button] = true;
            break;
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            io.MousePos.x = event->mouse_x;
            io.MousePos.y = event->mouse_y;
            break;
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            io.MouseWheelH = event->scroll_x;
            io.MouseWheel = event->scroll_y;
            break;
        default:
            break;
    }
}

// imgui draw callback
void imgui_draw_cb(ImDrawData* draw_data) {
    assert(draw_data);
    if (draw_data->CmdListsCount == 0) {
        return;
    }

    // copy vertices and indices
    int num_vertices = 0;
    int num_indices = 0;
    int num_cmdlists = 0;
    for (num_cmdlists = 0; num_cmdlists < draw_data->CmdListsCount; num_cmdlists++) {
        const ImDrawList* cl = draw_data->CmdLists[num_cmdlists];
        const int cl_num_vertices = cl->VtxBuffer.size();
        const int cl_num_indices = cl->IdxBuffer.size();

        // overflow check
        if ((num_vertices + cl_num_vertices) > MaxVertices) {
            break;
        }
        if ((num_indices + cl_num_indices) > MaxIndices) {
            break;
        }

        // copy vertices
        memcpy(&vertices[num_vertices], &cl->VtxBuffer.front(), cl_num_vertices*sizeof(ImDrawVert));

        // copy indices, need to 'rebase' indices to start of global vertex buffer
        const ImDrawIdx* src_index_ptr = &cl->IdxBuffer.front();
        const uint16_t base_vertex_index = num_vertices;
        for (int i = 0; i < cl_num_indices; i++) {
            indices[num_indices++] = src_index_ptr[i] + base_vertex_index;
        }
        num_vertices += cl_num_vertices;
    }

    // update vertex and index buffers
    const int vertex_data_size = num_vertices * sizeof(ImDrawVert);
    const int index_data_size = num_indices * sizeof(uint16_t);
    sg_update_buffer(draw_state.vertex_buffers[0], vertices, vertex_data_size);
    sg_update_buffer(draw_state.index_buffer, indices, index_data_size);

    // render the command list
    vs_params_t vs_params;
    vs_params.disp_size = ImGui::GetIO().DisplaySize;
    sg_apply_draw_state(&draw_state);
    sg_apply_uniform_block(SG_SHADERSTAGE_VS, 0, &vs_params, sizeof(vs_params));
    int base_element = 0;
    for (int cl_index=0; cl_index<num_cmdlists; cl_index++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[cl_index];
        for (const ImDrawCmd& pcmd : cmd_list->CmdBuffer) {
            if (pcmd.UserCallback) {
                pcmd.UserCallback(cmd_list, &pcmd);
            }
            else {
                const int sx = (int) pcmd.ClipRect.x;
                const int sy = (int) pcmd.ClipRect.y;
                const int sw = (int) (pcmd.ClipRect.z - pcmd.ClipRect.x);
                const int sh = (int) (pcmd.ClipRect.w - pcmd.ClipRect.y);
                sg_apply_scissor_rect(sx, sy, sw, sh, true);
                sg_draw(base_element, pcmd.ElemCount, 1);
            }
            base_element += pcmd.ElemCount;
        }
    }
}

sapp_desc sokol_main(int argc, char* argv[]) {
    sapp_desc desc = { };
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup;
    desc.event_cb = input;
    desc.width = 1024;
    desc.height = 768;
    desc.window_title = "Dear ImGui (sokol-sapp)";
    desc.html5_canvas_resize = true;
    return desc;
}

#if defined(SOKOL_GLCORE33)
const char* vs_src =
    "#version 330\n"
    "uniform vec2 disp_size;\n"
    "in vec2 position;\n"
    "in vec2 texcoord0;\n"
    "in vec4 color0;\n"
    "out vec2 uv;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "    gl_Position = vec4(((position/disp_size)-0.5)*vec2(2.0,-2.0), 0.5, 1.0);\n"
    "    uv = texcoord0;\n"
    "    color = color0;\n"
    "}\n";
const char* fs_src =
    "#version 330\n"
    "uniform sampler2D tex;\n"
    "in vec2 uv;\n"
    "in vec4 color;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    frag_color = texture(tex, uv) * color;\n"
    "}\n";
#elif defined(SOKOL_GLES3)
const char* vs_src =
    "#version 300 es\n"
    "uniform vec2 disp_size;\n"
    "in vec2 position;\n"
    "in vec2 texcoord0;\n"
    "in vec4 color0;\n"
    "out vec2 uv;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "    gl_Position = vec4(((position/disp_size)-0.5)*vec2(2.0,-2.0), 0.5, 1.0);\n"
    "    uv = texcoord0;\n"
    "    color = color0;\n"
    "}\n";
const char* fs_src =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform sampler2D tex;\n"
    "in vec2 uv;\n"
    "in vec4 color;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    frag_color = texture(tex, uv) * color;\n"
    "}\n";
#elif defined(SOKOL_METAL)
const char* vs_src =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct params_t {\n"
    "  float2 disp_size;\n"
    "};\n"
    "struct vs_in {\n"
    "  float2 pos [[attribute(0)]];\n"
    "  float2 uv [[attribute(1)]];\n"
    "  float4 color [[attribute(2)]];\n"
    "};\n"
    "struct vs_out {\n"
    "  float4 pos [[position]];\n"
    "  float2 uv;\n"
    "  float4 color;\n"
    "};\n"
    "vertex vs_out _main(vs_in in [[stage_in]], constant params_t& params [[buffer(0)]]) {\n"
    "  vs_out out;\n"
    "  out.pos = float4(((in.pos / params.disp_size)-0.5)*float2(2.0,-2.0), 0.5, 1.0);\n"
    "  out.uv = in.uv;\n"
    "  out.color = in.color;\n"
    "  return out;\n"
    "}\n";
const char* fs_src = 
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct fs_in {\n"
    "  float2 uv;\n"
    "  float4 color;\n"
    "};\n"
    "fragment float4 _main(fs_in in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {\n"
    "  return tex.sample(smp, in.uv) * in.color;\n"
    "}\n";
#endif