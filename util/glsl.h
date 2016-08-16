#ifndef __UTIL_GLSL_H
#define __UTIL_GLSL_H

#include "util/common.h"

struct shader_ref {
    class impl : public std::enable_shared_from_this<impl> {
        GLuint  m_id{0};
        explicit impl() = default;
        explicit impl(GLenum _type) : m_id(glCreateShader(_type)){}
        constexpr impl(impl &&o) noexcept
        : m_id(std::move(o.m_id)) { }
        impl &operator =(impl &&o) noexcept
        { using std::swap; swap(m_id,o.m_id);return *this;}
    public:
       ~impl() { glDeleteShader(m_id);}
        impl&operator=(GLuint _id)
        {
            if(!m_id) m_id = _id;
            return *this;
        }
        using pointer = std::shared_ptr<impl>;

        GLuint id() const { return m_id;}
        operator bool () const { return m_id;}
        bool operator !() const{return !m_id;}
        bool operator == ( const impl &o){return m_id == o.m_id;}
        operator const GLuint& () const { return m_id;}
        operator       GLuint& ()       { return m_id;}
        static pointer create() { return pointer(new impl());}
        static pointer create_from(GLuint _id) { auto ret = pointer(new impl()); *ret = _id;return ret;}
        static pointer create(GLenum type)    { return pointer(new impl(type)); }
    };
    using pointer = impl::pointer;
    pointer m_d{};

    explicit shader_ref(GLenum type) : m_d(impl::create(type)) { }
    explicit shader_ref()  : m_d(impl::create()){}
    shader_ref(const shader_ref &o) = default;
    shader_ref(shader_ref &&o) noexcept = default;
    shader_ref&operator = (const shader_ref &o) = default;
    shader_ref&operator = (shader_ref &&o) noexcept = default;
    shader_ref&operator = (GLuint _id)
    {
        if(*m_d) {
            m_d = impl::create_from(_id);
        }else{
            *m_d = _id;
        }
        return *this;
    }
    operator const GLuint& () const { return *m_d;}
    operator GLuint& () { return *m_d;}
    GLuint id() const { return m_d->id();}
    operator bool() const { return *m_d;}
    bool operator !() const { return !(*m_d);}
    void create(GLenum type) { m_d = impl::create(type); }
    void reset(GLuint _id) { m_d = impl::create_from(_id); }
    void reset() { m_d = impl::create();}
};

std::string get_load_shader_error(void);

bool   configure_vertex_area(float ww, float wh);
GLuint compile_shader(GLenum type, const std::string &file);
GLuint compile_shader_src(GLenum type, const std::string &src);

GLuint load_shader(const char * filename);
GLuint load_shader(const char *vert, const char * frag);
GLuint load_shader_noheader(const char *vert, const char * frag);
GLuint load_compute(const char *filename);

GLuint make_texture(int w, int h);
GLuint make_texture(GLenum format, int w, int h, int layers);
GLuint make_texture(GLenum format, int w, int h);
GLuint make_texture(int length);
#endif
