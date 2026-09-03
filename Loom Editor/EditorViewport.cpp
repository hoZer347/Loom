#include "EditorViewport.h"

#include "OpenGL.h"
#include "Engine.h"
#include "Scene.h"

#include <iostream>


namespace Loom
{
	EditorViewport::~EditorViewport()
	{
		Destroy();
	};

	void EditorViewport::Destroy()
	{
		if (m_fbo)		glDeleteFramebuffers(1, &m_fbo);
		if (m_texture)	glDeleteTextures(1, &m_texture);
		if (m_depth)	glDeleteRenderbuffers(1, &m_depth);

		m_fbo = m_texture = m_depth = 0;
		m_width = m_height = 0;
	};

	void EditorViewport::Resize(int width, int height)
	{
		if (m_fbo && width == m_width && height == m_height)
			return;

		Destroy();

		m_width = width;
		m_height = height;

		glGenFramebuffers(1, &m_fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

		glGenTextures(1, &m_texture);
		glBindTexture(GL_TEXTURE_2D, m_texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);

		glGenRenderbuffers(1, &m_depth);
		glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_width, m_height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depth);

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			std::cerr << "Editor viewport framebuffer is incomplete" << std::endl;
			Destroy();
		};

		glBindTexture(GL_TEXTURE_2D, 0);
		glBindRenderbuffer(GL_RENDERBUFFER, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	};

	void EditorViewport::Render(Scene* scene, int width, int height)
	{
		if (width <= 0 || height <= 0)
			return;

		Resize(width, height);

		if (!m_fbo)
			return;

		GLint previous_fbo = 0;
		GLint previous_viewport[4]{ };
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);
		glGetIntegerv(GL_VIEWPORT, previous_viewport);

		glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
		glViewport(0, 0, m_width, m_height);

		glClearColor(
			Engine::clearColor[0],
			Engine::clearColor[1],
			Engine::clearColor[2],
			Engine::clearColor[3]);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		if (scene)
			scene->Render();

		glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous_fbo);
		glViewport(
			previous_viewport[0],
			previous_viewport[1],
			previous_viewport[2],
			previous_viewport[3]);
	};

	void* EditorViewport::GetTextureID() const
	{
		return (void*)(intptr_t)m_texture;
	};
};
