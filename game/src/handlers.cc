#include <openrayman/game/handlers.h>

namespace openrayman::game
{
	namespace
	{
		namespace actor_field
		{
			constexpr std::uint32_t brain = 0x0C;
		}
		namespace mind_field
		{
			constexpr std::uint32_t normal = 0x04, reflex = 0x08, running_normal = 0x10;
		}

		std::uint32_t mind_of(interpreter& vm, std::uint32_t super_object)
		{
			live_memory& m = vm.memory();
			return m.u32(m.u32(vm.actor_of(super_object) + actor_field::brain));
		}

		// procedure 69 (0x4696C0): switch the actor's normal behaviour. From the normal behaviour
		// itself, its rules start over under the new one this same frame.
		std::uint32_t change_behaviour(call& c)
		{
			value behaviour;
			std::uint32_t next = c.vm.eval(c.self, c.args, behaviour);
			std::uint32_t mind = mind_of(c.vm, c.self);
			bool from_reflex = c.vm.memory().u8(mind + mind_field::running_normal) == 0;
			c.vm.change_behaviour(c.vm.memory().u32(mind + mind_field::normal), behaviour.word(), from_reflex);
			return next;
		}
	}

	void register_handlers(interpreter& vm)
	{
		vm.on_procedure(69, change_behaviour);
	}
}
