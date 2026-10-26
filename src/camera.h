#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace openrayman
{
	// A free-fly camera for looking around levels. The game's world is Z up.
	struct fly_camera
	{
		glm::vec3 position{ 0 };
		float yaw = 0;   // radians around Z; 0 looks along +X
		float pitch = 0; // radians; positive looks up
		float fov_y = glm::radians(60.0f);

		glm::vec3 forward() const
		{
			const float cos_pitch = glm::cos(pitch);
			return glm::normalize(glm::vec3(glm::cos(yaw) * cos_pitch, glm::sin(yaw) * cos_pitch, glm::sin(pitch)));
		}

		// Horizontal, so strafing never changes height.
		glm::vec3 right() const
		{
			return glm::vec3(glm::sin(yaw), -glm::cos(yaw), 0.0f);
		}

		glm::mat4 view() const
		{
			return glm::lookAt(position, position + forward(), glm::vec3(0, 0, 1));
		}

		glm::mat4 projection(float aspect) const
		{
			return glm::perspective(fov_y, aspect, 0.1f, 2000.0f);
		}

		// Mouse look, in pixels: right turns right, up looks up.
		void look(float dx, float dy)
		{
			constexpr float sensitivity = 0.0025f;
			yaw -= dx * sensitivity;
			pitch = glm::clamp(pitch - dy * sensitivity, -1.55f, 1.55f);
		}

		void move(float forward_amount, float right_amount, float up_amount)
		{
			position += forward() * forward_amount + right() * right_amount + glm::vec3(0, 0, 1) * up_amount;
		}
	};
}
