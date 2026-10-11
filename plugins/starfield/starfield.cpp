#include "plugin.hpp"
#include <GLES2/gl2.h>
#include <vector>
#include <random>
#include <cmath>
#include <iostream>

namespace {

struct Star {
    float x;
    float y;
    float z;
    float speed;
    float base_size;
    float r, g, b;
    float twinkle_phase;
    float twinkle_speed;
};

struct StarVertex {
    float x, y, z;
    float size;
    float r, g, b, a;
};

GLuint create_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "Starfield shader error: " << log << std::endl;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

class StarfieldPlugin : public Plugin {
    static constexpr int NUM_STARS = 1000;
    std::vector<Star> stars_;
    std::mt19937 rng_{42};

    int width_ = 1920;
    int height_ = 1080;

    GLuint program_ = 0;
    GLuint vbo_ = 0;

    GLint loc_u_aspect_ = -1;
    GLint loc_u_opacity_ = -1;

    void reset_star(Star& s, bool random_z) {
        std::uniform_real_distribution<float> dist_xy(-1.2f, 1.2f);
        std::uniform_real_distribution<float> dist_z(0.05f, 1.0f);
        std::uniform_real_distribution<float> dist_spd(0.18f, 0.55f);
        std::uniform_real_distribution<float> dist_size(12.0f, 32.0f);
        std::uniform_real_distribution<float> dist_twinkle(0.0f, 6.28f);
        std::uniform_real_distribution<float> dist_color(0.0f, 1.0f);

        s.x = dist_xy(rng_);
        s.y = dist_xy(rng_);
        s.z = random_z ? dist_z(rng_) : 1.0f;
        s.speed = dist_spd(rng_);
        s.base_size = dist_size(rng_);
        s.twinkle_phase = dist_twinkle(rng_);
        s.twinkle_speed = 2.0f + dist_color(rng_) * 4.0f;

        // Rich, vibrant cosmic palette for high visibility across all window colors
        float c = dist_color(rng_);
        if (c < 0.20f) {
            // Electric Neon Cyan / Aquamarine
            s.r = 0.05f; s.g = 0.95f; s.b = 1.0f;
        } else if (c < 0.40f) {
            // Cosmic Magenta / Hot Pink
            s.r = 1.0f; s.g = 0.15f; s.b = 0.85f;
        } else if (c < 0.60f) {
            // Radiant Amber / Stellar Gold
            s.r = 1.0f; s.g = 0.82f; s.b = 0.12f;
        } else if (c < 0.75f) {
            // Supernova Emerald
            s.r = 0.15f; s.g = 1.0f; s.b = 0.50f;
        } else if (c < 0.90f) {
            // Deep Electric Violet
            s.r = 0.80f; s.g = 0.30f; s.b = 1.0f;
        } else {
            // Diamond Brilliant Ice White
            s.r = 0.95f; s.g = 0.98f; s.b = 1.0f;
        }
    }

public:
    std::string name() const override { return "starfield"; }

    std::string description() const override {
        return "Vibrant 3D warp starfield with large, glowing colorful stars";
    }

    void init(int width, int height) override {
        width_ = width;
        height_ = height;

        stars_.resize(NUM_STARS);
        for (auto& s : stars_) {
            reset_star(s, true);
        }

        const char* vs_src = R"(#version 100
            attribute vec3 a_pos;
            attribute float a_size;
            attribute vec4 a_color;
            uniform float u_aspect;
            varying vec4 v_color;
            void main() {
                float inv_z = 1.0 / max(a_pos.z, 0.02);
                vec2 screen_pos = vec2(a_pos.x * inv_z / u_aspect, a_pos.y * inv_z);
                gl_Position = vec4(screen_pos, 0.0, 1.0);
                gl_PointSize = a_size;
                v_color = a_color;
            }
        )";

        const char* fs_src = R"(#version 100
            precision mediump float;
            varying vec4 v_color;
            uniform float u_opacity;
            void main() {
                vec2 coord = gl_PointCoord - vec2(0.5);
                float dist = length(coord);
                if (dist > 0.5) discard;

                float d = dist * 2.0;

                // Glowing core & rich saturated halo
                float core = clamp(1.0 - d * 2.2, 0.0, 1.0);
                float halo = clamp(1.0 - d, 0.0, 1.0);
                halo = halo * halo;

                // 4-point sparkle diffraction flare for near/large stars
                vec2 p = abs(coord);
                float spike = max(0.0, 1.0 - min(p.x, p.y) * 14.0) * max(0.0, 0.5 - dist) * 2.0;

                // Brilliant white-hot center blending into intense saturated neon color
                vec3 rgb = mix(v_color.rgb, vec3(1.0), core * 0.75 + spike * 0.45);

                // High alpha for crystal clarity over both dark and white backgrounds
                float alpha = clamp((halo * 0.80 + core * 0.95 + spike * 0.60) * v_color.a * u_opacity, 0.0, 0.98);

                // Premultiplied alpha for clean transparent overlay blending
                gl_FragColor = vec4(rgb * alpha, alpha);
            }
        )";

        GLuint vs = create_shader(GL_VERTEX_SHADER, vs_src);
        GLuint fs = create_shader(GL_FRAGMENT_SHADER, fs_src);
        program_ = glCreateProgram();
        glAttachShader(program_, vs);
        glAttachShader(program_, fs);
        glLinkProgram(program_);

        loc_u_aspect_ = glGetUniformLocation(program_, "u_aspect");
        loc_u_opacity_ = glGetUniformLocation(program_, "u_opacity");

        glGenBuffers(1, &vbo_);
    }

    void resize(int width, int height) override {
        width_ = width;
        height_ = height;
    }

    void render(float dt, float master_opacity) override {
        if (dt > 0.1f) dt = 0.016f;

        std::vector<StarVertex> vertices;
        vertices.reserve(NUM_STARS);

        for (auto& s : stars_) {
            s.z -= s.speed * dt;
            s.twinkle_phase += s.twinkle_speed * dt;

            if (s.z <= 0.02f) {
                reset_star(s, false);
            }

            // Depth-based size scaling (stars grow big and warp forward as z approaches 0)
            float depth_factor = clamp_float((1.0f - s.z) / 0.98f, 0.0f, 1.0f);
            float current_size = s.base_size * (0.4f + depth_factor * 1.8f);

            // Subtle twinkling pulse
            float twinkle = 0.85f + 0.15f * std::sin(s.twinkle_phase);
            float alpha = clamp_float(twinkle * (0.6f + depth_factor * 0.4f), 0.3f, 1.0f);

            vertices.push_back({s.x, s.y, s.z, current_size, s.r, s.g, s.b, alpha});
        }

        glViewport(0, 0, width_, height_);

        // Completely transparent background
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_BLEND);
        // Premultiplied alpha blending
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(program_);
        float aspect = height_ > 0 ? static_cast<float>(width_) / static_cast<float>(height_) : 1.0f;
        glUniform1f(loc_u_aspect_, aspect);
        glUniform1f(loc_u_opacity_, master_opacity);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(StarVertex), vertices.data(), GL_DYNAMIC_DRAW);

        GLint loc_pos = glGetAttribLocation(program_, "a_pos");
        GLint loc_size = glGetAttribLocation(program_, "a_size");
        GLint loc_color = glGetAttribLocation(program_, "a_color");

        glEnableVertexAttribArray(loc_pos);
        glVertexAttribPointer(loc_pos, 3, GL_FLOAT, GL_FALSE, sizeof(StarVertex), reinterpret_cast<void*>(0));

        glEnableVertexAttribArray(loc_size);
        glVertexAttribPointer(loc_size, 1, GL_FLOAT, GL_FALSE, sizeof(StarVertex), reinterpret_cast<void*>(3 * sizeof(float)));

        glEnableVertexAttribArray(loc_color);
        glVertexAttribPointer(loc_color, 4, GL_FLOAT, GL_FALSE, sizeof(StarVertex), reinterpret_cast<void*>(4 * sizeof(float)));

        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(vertices.size()));

        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void cleanup() override {
        if (vbo_) {
            glDeleteBuffers(1, &vbo_);
            vbo_ = 0;
        }
        if (program_) {
            glDeleteProgram(program_);
            program_ = 0;
        }
    }

private:
    static float clamp_float(float v, float mn, float mx) {
        return (v < mn) ? mn : (v > mx) ? mx : v;
    }
};

} // namespace

REGISTER_PLUGIN(StarfieldPlugin, "starfield", "Vibrant 3D warp starfield with large, glowing colorful stars");
