#include "plugin.hpp"
#include <GLES2/gl2.h>
#include <cmath>
#include <iostream>

namespace {

GLuint create_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "Aurora shader error: " << log << std::endl;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

class AuroraPlugin : public Plugin {
    int width_ = 1920;
    int height_ = 1080;
    float time_ = 0.0f;

    GLuint program_ = 0;
    GLuint vbo_ = 0;

    GLint loc_u_resolution_ = -1;
    GLint loc_u_time_ = -1;
    GLint loc_u_opacity_ = -1;

public:
    std::string name() const override { return "aurora"; }

    std::string description() const override {
        return "Flowing cosmic aurora and psychedelic energy ribbons inspired by Van Gogh's Starry Night";
    }

    void init(int width, int height) override {
        width_ = width;
        height_ = height;

        const char* vs_src = R"(#version 100
            attribute vec2 a_pos;
            varying vec2 v_uv;
            void main() {
                v_uv = a_pos * 0.5 + 0.5;
                gl_Position = vec4(a_pos, 0.0, 1.0);
            }
        )";

        const char* fs_src = R"(#version 100
            precision mediump float;
            varying vec2 v_uv;
            uniform vec2 u_resolution;
            uniform float u_time;
            uniform float u_opacity;

            void main() {
                vec2 uv = (gl_FragCoord.xy / u_resolution.xy) * 2.0 - 1.0;
                uv.x *= (u_resolution.x / u_resolution.y);

                float t = u_time * 0.4;

                // 3 undulating harmonic waves
                float wave1 = sin(uv.x * 1.5 + t * 1.2) * 0.35 + cos(uv.x * 0.8 - t * 0.6) * 0.2;
                float wave2 = cos(uv.x * 2.0 - t * 0.9) * 0.30 + sin(uv.x * 1.2 + t * 0.7) * 0.25;
                float wave3 = sin(uv.x * 2.8 + t * 1.5) * 0.20 + cos(uv.x * 1.6 - t * 1.1) * 0.2;

                // Vertical distance from ribbons
                float d1 = abs(uv.y - wave1 + 0.2);
                float d2 = abs(uv.y - wave2 - 0.1);
                float d3 = abs(uv.y - wave3 + 0.35);

                // Ribbon intensities with soft glowing falloff
                float r1 = exp(-d1 * 6.0) * 0.85;
                float r2 = exp(-d2 * 5.5) * 0.75;
                float r3 = exp(-d3 * 7.0) * 0.90;

                // Vibrant Starry Night color palette:
                // Ribbon 1: Electric Teal / Cyan
                vec3 col1 = vec3(0.05, 0.95, 0.85);
                // Ribbon 2: Cosmic Magenta / Violet
                vec3 col2 = vec3(0.85, 0.20, 0.95);
                // Ribbon 3: Van Gogh Golden Amber
                vec3 col3 = vec3(1.0, 0.75, 0.15);

                vec3 col = col1 * r1 + col2 * r2 + col3 * r3;
                float total_alpha = clamp((r1 + r2 + r3) * 0.75 * u_opacity, 0.0, 0.90);

                if (total_alpha < 0.01) discard;

                gl_FragColor = vec4(col * total_alpha, total_alpha);
            }
        )";

        GLuint vs = create_shader(GL_VERTEX_SHADER, vs_src);
        GLuint fs = create_shader(GL_FRAGMENT_SHADER, fs_src);
        program_ = glCreateProgram();
        glAttachShader(program_, vs);
        glAttachShader(program_, fs);
        glLinkProgram(program_);

        loc_u_resolution_ = glGetUniformLocation(program_, "u_resolution");
        loc_u_time_ = glGetUniformLocation(program_, "u_time");
        loc_u_opacity_ = glGetUniformLocation(program_, "u_opacity");

        // Fullscreen quad [-1, -1] to [1, 1]
        float quad_verts[] = {
            -1.0f, -1.0f,
             1.0f, -1.0f,
            -1.0f,  1.0f,
            -1.0f,  1.0f,
             1.0f, -1.0f,
             1.0f,  1.0f
        };

        glGenBuffers(1, &vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts), quad_verts, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void resize(int width, int height) override {
        width_ = width;
        height_ = height;
    }

    void render(float dt, float master_opacity) override {
        if (dt > 0.1f) dt = 0.016f;
        time_ += dt;

        glViewport(0, 0, width_, height_);

        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(program_);
        glUniform2f(loc_u_resolution_, static_cast<float>(width_), static_cast<float>(height_));
        glUniform1f(loc_u_time_, time_);
        glUniform1f(loc_u_opacity_, master_opacity);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        GLint loc_pos = glGetAttribLocation(program_, "a_pos");
        glEnableVertexAttribArray(loc_pos);
        glVertexAttribPointer(loc_pos, 2, GL_FLOAT, GL_FALSE, 0, reinterpret_cast<void*>(0));

        glDrawArrays(GL_TRIANGLES, 0, 6);

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

REGISTER_PLUGIN(AuroraPlugin, "aurora", "Flowing cosmic aurora and psychedelic energy ribbons inspired by Van Gogh's Starry Night");
