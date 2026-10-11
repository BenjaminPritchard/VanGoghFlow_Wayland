#include "plugin.hpp"
#include <GLES2/gl2.h>
#include <vector>
#include <random>
#include <cmath>
#include <iostream>

namespace {

struct Firefly {
    float x, y;
    float vx, vy;
    float size;
    float pulse_phase;
    float pulse_speed;
    float r, g, b;
};

struct FireflyVertex {
    float x, y;
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
        std::cerr << "Fireflies shader error: " << log << std::endl;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

class FirefliesPlugin : public Plugin {
    static constexpr int NUM_FIREFLIES = 140;
    std::vector<Firefly> fireflies_;
    std::mt19937 rng_{999};

    int width_ = 1920;
    int height_ = 1080;

    GLuint program_ = 0;
    GLuint vbo_ = 0;

    GLint loc_u_aspect_ = -1;
    GLint loc_u_opacity_ = -1;

    void reset_firefly(Firefly& f) {
        std::uniform_real_distribution<float> dist_pos(-1.1f, 1.1f);
        std::uniform_real_distribution<float> dist_vel(-0.06f, 0.06f);
        std::uniform_real_distribution<float> dist_size(18.0f, 44.0f);
        std::uniform_real_distribution<float> dist_phase(0.0f, 6.28f);
        std::uniform_real_distribution<float> dist_spd(1.2f, 3.2f);
        std::uniform_real_distribution<float> dist_c(0.0f, 1.0f);

        f.x = dist_pos(rng_);
        f.y = dist_pos(rng_);
        f.vx = dist_vel(rng_);
        f.vy = dist_vel(rng_);
        f.size = dist_size(rng_);
        f.pulse_phase = dist_phase(rng_);
        f.pulse_speed = dist_spd(rng_);

        // Warm luminous jewel tones: Amber gold, neon coral, emerald jade, cyan
        float c = dist_c(rng_);
        if (c < 0.35f) {
            // Warm Golden Amber
            f.r = 1.0f; f.g = 0.78f; f.b = 0.15f;
        } else if (c < 0.60f) {
            // Neon Coral / Sunset Tangerine
            f.r = 1.0f; f.g = 0.35f; f.b = 0.40f;
        } else if (c < 0.80f) {
            // Luminescent Emerald Jade
            f.r = 0.25f; f.g = 1.0f; f.b = 0.45f;
        } else {
            // Celestial Aquamarine
            f.r = 0.10f; f.g = 0.90f; f.b = 1.0f;
        }
    }

public:
    std::string name() const override { return "fireflies"; }

    std::string description() const override {
        return "Luminous floating neon fireflies and gentle drifting orbs";
    }

    void init(int width, int height) override {
        width_ = width;
        height_ = height;

        fireflies_.resize(NUM_FIREFLIES);
        for (auto& f : fireflies_) {
            reset_firefly(f);
        }

        const char* vs_src = R"(#version 100
            attribute vec2 a_pos;
            attribute float a_size;
            attribute vec4 a_color;
            uniform float u_aspect;
            varying vec4 v_color;
            void main() {
                gl_Position = vec4(a_pos.x / u_aspect, a_pos.y, 0.0, 1.0);
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

                // Soft glowing bokeh orb falloff
                float core = clamp(1.0 - d * 2.4, 0.0, 1.0);
                float glow = clamp(1.0 - d, 0.0, 1.0);
                glow = glow * glow;

                vec3 rgb = mix(v_color.rgb, vec3(1.0), core * 0.7);
                float alpha = clamp((glow * 0.85 + core * 0.95) * v_color.a * u_opacity, 0.0, 0.92);

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

        std::vector<FireflyVertex> vertices;
        vertices.reserve(NUM_FIREFLIES);

        std::uniform_real_distribution<float> nudge(-0.08f, 0.08f);

        for (auto& f : fireflies_) {
            // Gentle Brownian wandering motion
            f.vx += nudge(rng_) * dt;
            f.vy += nudge(rng_) * dt;

            // Dampen velocity to keep smooth floating
            f.vx *= 0.985f;
            f.vy *= 0.985f;

            f.x += f.vx * dt;
            f.y += f.vy * dt;

            // Bounce gently off boundaries
            if (f.x < -1.2f) { f.x = -1.2f; f.vx = std::abs(f.vx); }
            if (f.x >  1.2f) { f.x =  1.2f; f.vx = -std::abs(f.vx); }
            if (f.y < -1.2f) { f.y = -1.2f; f.vy = std::abs(f.vy); }
            if (f.y >  1.2f) { f.y =  1.2f; f.vy = -std::abs(f.vy); }

            f.pulse_phase += f.pulse_speed * dt;
            float pulse = 0.65f + 0.35f * std::sin(f.pulse_phase);
            float current_size = f.size * (0.85f + 0.25f * pulse);

            vertices.push_back({f.x, f.y, current_size, f.r, f.g, f.b, pulse});
        }

        glViewport(0, 0, width_, height_);

        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(program_);
        float aspect = height_ > 0 ? static_cast<float>(width_) / static_cast<float>(height_) : 1.0f;
        glUniform1f(loc_u_aspect_, aspect);
        glUniform1f(loc_u_opacity_, master_opacity);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(FireflyVertex), vertices.data(), GL_DYNAMIC_DRAW);

        GLint loc_pos = glGetAttribLocation(program_, "a_pos");
        GLint loc_size = glGetAttribLocation(program_, "a_size");
        GLint loc_color = glGetAttribLocation(program_, "a_color");

        glEnableVertexAttribArray(loc_pos);
        glVertexAttribPointer(loc_pos, 2, GL_FLOAT, GL_FALSE, sizeof(FireflyVertex), reinterpret_cast<void*>(0));

        glEnableVertexAttribArray(loc_size);
        glVertexAttribPointer(loc_size, 1, GL_FLOAT, GL_FALSE, sizeof(FireflyVertex), reinterpret_cast<void*>(2 * sizeof(float)));

        glEnableVertexAttribArray(loc_color);
        glVertexAttribPointer(loc_color, 4, GL_FLOAT, GL_FALSE, sizeof(FireflyVertex), reinterpret_cast<void*>(3 * sizeof(float)));

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
};

} // namespace

REGISTER_PLUGIN(FirefliesPlugin, "fireflies", "Luminous floating neon fireflies and gentle drifting orbs");
