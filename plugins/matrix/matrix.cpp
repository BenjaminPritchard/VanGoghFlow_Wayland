#include "plugin.hpp"
#include <GLES2/gl2.h>
#include <vector>
#include <random>
#include <cmath>
#include <iostream>

namespace {

struct Drop {
    float x;           // X in [-1.0, 1.0]
    float y;           // Y in [-1.2, 1.2]
    float speed;
    float length;
    float glyph_size;
    float brightness;
};

struct MatrixVertex {
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
        std::cerr << "Matrix shader error: " << log << std::endl;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

class MatrixPlugin : public Plugin {
    static constexpr int NUM_COLUMNS = 75;
    std::vector<Drop> drops_;
    std::mt19937 rng_{1337};

    int width_ = 1920;
    int height_ = 1080;

    GLuint program_ = 0;
    GLuint vbo_ = 0;

    GLint loc_u_opacity_ = -1;

    void reset_drop(Drop& d, bool randomize_y) {
        std::uniform_real_distribution<float> dist_spd(0.4f, 1.1f);
        std::uniform_real_distribution<float> dist_len(0.25f, 0.65f);
        std::uniform_real_distribution<float> dist_size(14.0f, 22.0f);
        std::uniform_real_distribution<float> dist_bright(0.7f, 1.0f);
        std::uniform_real_distribution<float> dist_y(-1.2f, 1.2f);

        d.speed = dist_spd(rng_);
        d.length = dist_len(rng_);
        d.glyph_size = dist_size(rng_);
        d.brightness = dist_bright(rng_);
        d.y = randomize_y ? dist_y(rng_) : 1.2f;
    }

public:
    std::string name() const override { return "matrix"; }

    std::string description() const override {
        return "Cyberpunk digital rain with cascading glowing code glyphs";
    }

    void init(int width, int height) override {
        width_ = width;
        height_ = height;

        drops_.resize(NUM_COLUMNS);
        for (int i = 0; i < NUM_COLUMNS; ++i) {
            float x_norm = ((float)i / (float)NUM_COLUMNS) * 2.0f - 1.0f;
            drops_[i].x = x_norm;
            reset_drop(drops_[i], true);
        }

        const char* vs_src = R"(#version 100
            attribute vec2 a_pos;
            attribute float a_size;
            attribute vec4 a_color;
            varying vec4 v_color;
            void main() {
                gl_Position = vec4(a_pos, 0.0, 1.0);
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

                // Sharp digital glyph shape with soft glowing edge
                float d = dist * 2.0;
                float body = clamp(1.0 - d, 0.0, 1.0);
                body = body * body;

                // Center core
                float core = clamp(1.0 - d * 2.5, 0.0, 1.0);

                vec3 rgb = mix(v_color.rgb, vec3(1.0), core * 0.7);
                float alpha = clamp((body * 0.9 + core * 0.9) * v_color.a * u_opacity, 0.0, 1.0);

                gl_FragColor = vec4(rgb * alpha, alpha);
            }
        )";

        GLuint vs = create_shader(GL_VERTEX_SHADER, vs_src);
        GLuint fs = create_shader(GL_FRAGMENT_SHADER, fs_src);
        program_ = glCreateProgram();
        glAttachShader(program_, vs);
        glAttachShader(program_, fs);
        glLinkProgram(program_);

        loc_u_opacity_ = glGetUniformLocation(program_, "u_opacity");
        glGenBuffers(1, &vbo_);
    }

    void resize(int width, int height) override {
        width_ = width;
        height_ = height;
    }

    void render(float dt, float master_opacity) override {
        if (dt > 0.1f) dt = 0.016f;

        std::vector<MatrixVertex> vertices;
        // Each drop generates ~12 trail glyphs
        vertices.reserve(NUM_COLUMNS * 15);

        for (auto& d : drops_) {
            d.y -= d.speed * dt;
            if (d.y + d.length < -1.2f) {
                reset_drop(d, false);
            }

            int trail_steps = 14;
            float step_dy = d.length / static_cast<float>(trail_steps);

            for (int t = 0; t < trail_steps; ++t) {
                float gy = d.y + t * step_dy;
                if (gy < -1.2f || gy > 1.2f) continue;

                float t_norm = static_cast<float>(t) / static_cast<float>(trail_steps); // 0 at head, 1 at tail

                float r, g, b, a;
                if (t == 0) {
                    // Brilliant White/Cyan leading spark head
                    r = 0.85f; g = 1.0f; b = 0.95f; a = 1.0f * d.brightness;
                } else if (t == 1) {
                    // Bright neon cyan-green
                    r = 0.2f; g = 1.0f; b = 0.7f; a = 0.95f * d.brightness;
                } else {
                    // Cascading digital emerald green trail fading out
                    float fade = std::pow(1.0f - t_norm, 1.5f);
                    r = 0.05f * fade;
                    g = 0.95f * fade;
                    b = 0.25f * fade;
                    a = fade * 0.9f * d.brightness;
                }

                float size = d.glyph_size * (1.1f - t_norm * 0.4f);
                vertices.push_back({d.x, gy, size, r, g, b, a});
            }
        }

        glViewport(0, 0, width_, height_);

        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(program_);
        glUniform1f(loc_u_opacity_, master_opacity);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(MatrixVertex), vertices.data(), GL_DYNAMIC_DRAW);

        GLint loc_pos = glGetAttribLocation(program_, "a_pos");
        GLint loc_size = glGetAttribLocation(program_, "a_size");
        GLint loc_color = glGetAttribLocation(program_, "a_color");

        glEnableVertexAttribArray(loc_pos);
        glVertexAttribPointer(loc_pos, 2, GL_FLOAT, GL_FALSE, sizeof(MatrixVertex), reinterpret_cast<void*>(0));

        glEnableVertexAttribArray(loc_size);
        glVertexAttribPointer(loc_size, 1, GL_FLOAT, GL_FALSE, sizeof(MatrixVertex), reinterpret_cast<void*>(2 * sizeof(float)));

        glEnableVertexAttribArray(loc_color);
        glVertexAttribPointer(loc_color, 4, GL_FLOAT, GL_FALSE, sizeof(MatrixVertex), reinterpret_cast<void*>(3 * sizeof(float)));

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

REGISTER_PLUGIN(MatrixPlugin, "matrix", "Cyberpunk digital rain with cascading glowing code glyphs");
