import Geometry;

#include "imgui.h"

#include <iostream>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "stb_image.h"

import <glm/glm.hpp>;
using namespace glm;


namespace Loom::Geometry
{
	Rect::Rect(
		const float& x,
		const float& y,
		const float& w,
		const float& h,
		const vec4& color) :
		x(x),
		y(y),
		w(w),
		h(h),
		color(color)
	{ };

	Rect::~Rect()
	{ };

	void Rect::OnRender()
	{
		if (GLFWwindow* window = glfwGetCurrentContext())
		{
			int w_w, w_h;
			glfwGetWindowSize(window, &w_w, &w_h);

			glBegin(GL_POLYGON);
			glColor4f(color.r, color.g, color.b, color.a);
			glVertex2f(x / w_w, y / w_h);
			glVertex2f(x / w_w, (y + h) / w_h);
			glVertex2f((x + w) / w_w, (y + h) / w_h);
			glVertex2f((x + w) / w_w, y / w_h);
			glEnd();
		};
	};

	void Rect::OnGui()
	{
		if (GLFWwindow* window = glfwGetCurrentContext())
		{
			int w_w, w_h;
			glfwGetWindowSize(window, &w_w, &w_h);

			ImGui::Text("Rect: ");
			ImGui::PushItemWidth(100);
			ImGui::PushID(this);
			ImGui::DragFloat("X: ", &x, 1);
			ImGui::SameLine();
			ImGui::DragFloat("Y: ", &y, 1);
			ImGui::DragFloat("W: ", &w, 1);
			ImGui::SameLine();
			ImGui::DragFloat("H: ", &h, 1);
			ImGui::PopItemWidth();
			ImGui::PopID();
		};
	};
};
