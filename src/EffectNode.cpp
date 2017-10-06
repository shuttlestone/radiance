#include "EffectNode.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QRegularExpression>
#include <memory>
#include <utility>
#include <functional>
#include <algorithm>
#include "main.h"

EffectNode::EffectNode()
    : m_openGLWorker(new EffectNodeOpenGLWorker(this))
    , m_intensity(0)
    , m_ready(false) {

    setInputCount(1);
    m_periodic.setInterval(10);
    m_periodic.start();
    connect(&m_periodic, &QTimer::timeout, this, &EffectNode::periodic);
    connect(m_openGLWorker.data(), &EffectNodeOpenGLWorker::initialized, this, &EffectNode::onInitialized);
}

EffectNode::EffectNode(const EffectNode &other)
    : VideoNode(other)
    , m_openGLWorker(other.m_openGLWorker)
    , m_intensity(other.m_intensity)
    , m_intensityIntegral(other.m_intensityIntegral)
    , m_programs(other.m_programs)
    , m_ready(other.m_ready) {

    auto k = other.m_renderStates.keys();
    for (int i=0; i<k.count(); i++) {
        auto otherRenderState = other.m_renderStates.value(k.at(i));
        m_renderStates.insert(k.at(i), QSharedPointer<EffectNodeRenderState>(new EffectNodeRenderState(*otherRenderState)));
    }
}

EffectNode::~EffectNode() {
}

void EffectNode::onInitialized() {
    m_ready = true;
}

void EffectNode::chainsEdited(QList<QSharedPointer<Chain>> added, QList<QSharedPointer<Chain>> removed) {
    QMutexLocker locker(&m_stateLock);
    for (int i=0; i<added.count(); i++) {
        m_renderStates.insert(added.at(i), QSharedPointer<EffectNodeRenderState>(new EffectNodeRenderState()));
    }
    for (int i=0; i<removed.count(); i++) {
        m_renderStates.remove(removed.at(i));
    }
}

// Paint never needs to take the stateLock
// because it already has a copy of the object
// and this copy will never be modified
// out from under it, unlike the parent
// from which it was created
GLuint EffectNode::paint(QSharedPointer<Chain> chain, QVector<GLuint> inputTextures) {
    // Hitting this assert means
    // that you failed to make a copy
    // of the VideoNode
    // before rendering in a different thread
    Q_ASSERT(QThread::currentThread() == thread());

    GLuint outTexture = 0;

    if (!m_ready) {
        qDebug() << this << "is not ready";
        return outTexture;
    }
    if (!m_renderStates.contains(chain)) {
        qDebug() << this << "does not have chain" << chain;
        return outTexture;
    }
    auto renderState = m_renderStates[chain];

    // FBO creation must happen here, and not in initialize,
    // because FBOs are not shared among contexts.
    // Textures are, however, so in the future maybe we can move
    // texture creation to initialize()
    // and leave lightweight FBO creation here
    if(renderState->m_intermediate.isEmpty()) {
        auto fmt = QOpenGLFramebufferObjectFormat{};
        fmt.setInternalTextureFormat(GL_RGBA);
        for(int i = 0; i < m_programs.count() + 1; i++) {
            auto fbo = QSharedPointer<QOpenGLFramebufferObject>::create(chain->size(),fmt);
            renderState->m_intermediate.append(fbo);
        }
    }

    glClearColor(0, 0, 0, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    {
        auto chanTex = std::make_unique<GLuint[]>(m_programs.size());
        std::iota(&chanTex[0], &chanTex[0] + m_programs.size(), 1 + m_inputCount);
        auto inputTex = std::make_unique<GLuint[]>(m_inputCount);
        std::iota(&inputTex[0], &inputTex[0] + m_inputCount, 0);
        auto   time = timebase->beat();
        m_realTimeLast = m_realTime;
        m_realTime     = timebase->wallTime();
        auto   step = m_realTime - m_realTimeLast;
        double audioHi = 0;
        double audioMid = 0;
        double audioLow = 0;
        double audioLevel = 0;
        audio->levels(&audioHi, &audioMid, &audioLow, &audioLevel);

        auto size = chain->size();
        glViewport(0, 0, size.width(), size.height());

        for(int j = m_programs.count() - 1; j >= 0; j--) {
            //qDebug() << "Rendering shader" << j << "onto" << (renderState->m_textureIndex + j + 1) % (m_programs.count() + 1);
            auto fboIndex = (renderState->m_textureIndex + j + 1) % (m_programs.size() + 1);
            auto & p = m_programs.at(j);

            p->bind();
            renderState->m_intermediate.at(fboIndex)->bind();

            for (int k = 0; k < m_inputCount; k++) {
                glActiveTexture(GL_TEXTURE0 + k);
                glBindTexture(GL_TEXTURE_2D, inputTextures.at(k));
            }

            glActiveTexture(GL_TEXTURE0 + m_inputCount);
            glBindTexture(GL_TEXTURE_2D, chain->noiseTexture());
            for(int k=0; k<m_programs.size(); k++) {
                glActiveTexture(GL_TEXTURE1 + m_inputCount + k);
                glBindTexture(GL_TEXTURE_2D, renderState->m_intermediate.at((renderState->m_textureIndex + k + (j < k)) % (m_programs.size() + 1))->texture());
                //qDebug() << "Bind" << (renderState->m_textureIndex + k + (j < k)) % (m_programs.count() + 1) << "as chan" << k;
            }
            auto intense = qreal(intensity());
            p->setUniformValue("iIntensity", GLfloat(intense));
            p->setUniformValue("iIntensityIntegral", GLfloat(m_intensityIntegral));
            p->setUniformValue("iStep", GLfloat(step));
            p->setUniformValue("iTime", GLfloat(time));
            p->setUniformValue("iFPS",  GLfloat(FPS));
            p->setUniformValue("iAudio", QVector4D(GLfloat(audioLow),GLfloat(audioMid),GLfloat(audioHi),GLfloat(audioLevel)));
            p->setUniformValueArray("iInputs", &inputTex[0], m_inputCount);
            p->setUniformValue("iNoise", m_inputCount);
            p->setUniformValue("iResolution", GLfloat(size.width()), GLfloat(size.height()));
            p->setUniformValueArray("iChannel", &chanTex[0], m_programs.size());
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            renderState->m_intermediate.at(fboIndex)->release();
            //renderState->m_intermediate.at(fboIndex)->toImage().save(QString("out_%1.png").arg(renderState->m_intermediate.at(fboIndex)->texture()));
            p->release();
            glActiveTexture(GL_TEXTURE0); // Very important to reset OpenGL state for scene graph rendering
        }

        outTexture = renderState->m_intermediate.at((renderState->m_textureIndex + 1) % (m_programs.size() + 1))->texture();
        //qDebug() << this << "Output texture ID is" << outTexture << renderState;
        //qDebug() << "Output is" << ((renderState->m_textureIndex + 1) % (m_programs.count() + 1));
        renderState->m_textureIndex = (renderState->m_textureIndex + 1) % (m_programs.size() + 1);
    }
    return outTexture;
}

void EffectNode::periodic() {
    Q_ASSERT(QThread::currentThread() == thread());
    QMutexLocker locker(&m_stateLock);
    m_intensityIntegral = fmod(m_intensityIntegral + m_intensity / FPS, MAX_INTEGRAL);
}

qreal EffectNode::intensity() {
    Q_ASSERT(QThread::currentThread() == thread());
    return m_intensity;
}

void EffectNode::setIntensity(qreal value) {
    {
        QMutexLocker locker(&m_stateLock);
        if(value > 1) value = 1;
        if(value < 0) value = 0;
        if(m_intensity == value)
            return;
        m_intensity = value;
    }
    emit intensityChanged(value);
}

QString EffectNode::name() {
    Q_ASSERT(QThread::currentThread() == thread());
    return m_name;
}

void EffectNode::setName(QString name) {
    Q_ASSERT(QThread::currentThread() == thread());
    if(name != m_name) {
        m_ready = false;
        {
            QMutexLocker locker(&m_stateLock);
            m_name = name;
        }
        bool result = QMetaObject::invokeMethod(m_openGLWorker.data(), "initialize");
        Q_ASSERT(result);
        emit nameChanged(name);
    }
}

void EffectNode::reload() {
    m_ready = false;
    bool result = QMetaObject::invokeMethod(m_openGLWorker.data(), "initialize");
    Q_ASSERT(result);
}

// Creates a copy of this node for rendering
QSharedPointer<VideoNode> EffectNode::createCopyForRendering() {
    QMutexLocker locker(&m_stateLock);
    QSharedPointer<VideoNode> v(new EffectNode(*this));
    v->moveToThread(QThread::currentThread()); // Move it to the thread that rendering will take place in
    return v;
}

// Reads back the new render state
void EffectNode::copyBackRenderState(QSharedPointer<Chain> chain, QSharedPointer<VideoNode> copy) {
    QSharedPointer<EffectNode> c = qSharedPointerCast<EffectNode>(copy);
    QMutexLocker locker(&m_stateLock);
    if (m_renderStates.contains(chain)) {
        *m_renderStates[chain] = *(c->m_renderStates.value(chain));
    } else {
        qDebug() << "Chain was deleted during rendering";
    }
}

// EffectNodeOpenGLWorker methods

EffectNodeOpenGLWorker::EffectNodeOpenGLWorker(EffectNode *p)
    : OpenGLWorker(openGLWorkerContext)
    , m_p(p) {
    connect(this, &EffectNodeOpenGLWorker::message, m_p, &EffectNode::message);
    connect(this, &EffectNodeOpenGLWorker::warning, m_p, &EffectNode::warning);
    connect(this, &EffectNodeOpenGLWorker::fatal,   m_p, &EffectNode::fatal);
}

void EffectNodeOpenGLWorker::initialize() {
    makeCurrent();
    bool result = loadProgram(m_p->m_name);
    if(!result) {
        qDebug() << m_p << "Load program failed :(";
        return;
    }
    glFlush();
    emit initialized();
}

// Call this to load shader code into this Effect.
// Returns true if the program was loaded successfully
bool EffectNodeOpenGLWorker::loadProgram(QString name) {
    Q_ASSERT(!m_p->m_ready); // Must have been marked unready

    auto headerString = QString{};
    {
        QFile header_file("../resources/glsl/effect_header.glsl");
        if(!header_file.open(QIODevice::ReadOnly)) {
            emit fatal(("Could not open \"../resources/effect_header.glsl\""));
            return false;
        }
        QTextStream headerStream(&header_file);
        headerString = headerStream.readAll();
    }
    auto vertexString = QString{
        "#version 130\n"
        "#extension GL_ARB_shading_language_420pack : enable\n"
        "const vec2 varray[4] = { vec2( 1., 1.),vec2(1., -1.),vec2(-1., 1.),vec2(-1., -1.)};\n"
        "out vec2 coords;\n"
        "void main() {"
        "    vec2 vertex = varray[gl_VertexID];\n"
        "    gl_Position = vec4(vertex,0.,1.);\n"
        "    coords = vertex;\n"
        "}"};
    auto programs = QVector<QSharedPointer<QOpenGLShaderProgram>>{};
    auto filename = QString("../resources/effects/%1.glsl").arg(name);

    QFileInfo check_file(filename);
    if(!(check_file.exists() && check_file.isFile())) {
        emit fatal(QString("Could not open \"%1\"").arg(filename));
        return false;
    }

    QFile file(filename);
    if(!file.open(QIODevice::ReadOnly)) {
        emit fatal(QString("Could not open \"%1\"").arg(filename));
        return false;
    }

    QTextStream stream(&file);

    auto buffershader_reg = QRegularExpression(
        "^\\s*#buffershader\\s*$"
      , QRegularExpression::CaseInsensitiveOption
        );
    auto property_reg = QRegularExpression(
        "^\\s*#property\\s+(?<name>\\w+)\\s+(?<value>.*)$"
      , QRegularExpression::CaseInsensitiveOption
        );
    auto passes = QVector<QStringList>{QStringList{"#line 0"}};
    auto props  = QMap<QString,QString>{{"inputCount","1"}};
    auto lineno = 0;
    for(auto next_line = QString{}; stream.readLineInto(&next_line);++lineno) {
        {
            auto m = property_reg.match(next_line);
            if(m.hasMatch()) {
                props.insert(m.captured("name"),m.captured("value"));
                passes.back().append(QString{"#line %1"}.arg(lineno));
                qDebug() << "setting property " << m.captured("name") << " to value " << m.captured("value");
                continue;
            }
        }
        {
            auto m = buffershader_reg.match(next_line);
            if(m.hasMatch()) {
                passes.append({QString{"#line %1"}.arg(lineno)});
                continue;
            }
        }
        passes.back().append(next_line);
    }

    for(auto pass : passes) {
        programs.append(QSharedPointer<QOpenGLShaderProgram>::create());
        auto && program = programs.back();
        if(!program->addShaderFromSourceCode(
            QOpenGLShader::Vertex
          , vertexString
            )) {
            emit fatal("Could not compile vertex shader");
            return false;
        }
        auto frag = headerString + "\n" + pass.join("\n");
        if(!program->addShaderFromSourceCode(QOpenGLShader::Fragment, frag)) {
            emit fatal("Could not compile fragment shader");
            return false;
        }
        if(!program->link()) {
            emit fatal("Could not link shader program");
            return false;
        }
    }
    if(programs.empty()) {
        emit fatal(QString("No shaders found for \"%1\"").arg(name));
        return false;
    }
/*    auto mo = m_p->metaObject();
    if(!props.isEmpty()) {
        for(auto i = props.cbegin(),e = props.cend(); i!= e; ++i) {
            auto prop_name = i.key().trimmed().toLocal8Bit();
            auto mpi = mo->indexOfProperty(prop_name.constData());
            if(mpi < 0) {
                qDebug() << "couldn't find property" << prop_name.constData();
                continue;
            }
            auto mp  = mo->property(mpi);
            if(!mp.isValid()) {
                qDebug() << "not a valid property" << prop_name.constData();
                continue;
            }
            auto success = mp.write(m_p, i.value());
            if(success) {
                qDebug() << "wrote " << mp.name() << " to value " << i.value();
            }else{
                qDebug() << "failed to write " << mp.name() << " to value " << i.value();

            }
        }
    }*/
    {
        QMutexLocker locker(&m_p->m_stateLock);
        m_p->m_programs = programs;
    }
    return true;
}

