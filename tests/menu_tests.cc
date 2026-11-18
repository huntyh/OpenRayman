#include "test_util.h"
#include <openrayman/game/dialog.h>
#include <openrayman/game/input.h>
#include <openrayman/game/handlers.h>
#include <openrayman/game/animation_player.h>
#include <openrayman/game/interpreter.h>
#include <openrayman/game/menu.h>
#include <openrayman/game/options.h>
#include <openrayman/game/saves.h>
#include <openrayman/game/sound_slots.h>
#include <openrayman/game/ui_text.h>
#include <doctest/doctest.h>
#include <random>

using namespace openrayman;

TEST_CASE("Every custom UI key has five translations and Options follows the language")
{
	for(const auto& row : game::ui_translations()) for(auto text : row) CHECK_FALSE(text.empty());
	for(unsigned language = 0; language < 5; ++language) {
		game::options_page page; game::engine_options options; options.language = int(language);
		game::screen_text slots; page.draw(slots,options,false,0);
		CHECK(slots.at(1).text == "/C:" + game::ui_text(game::ui_key::sound,language));
		CHECK(slots.at(1).size == 20);
		game::save_session session; session.confirmation_kind = 4;
		auto prompt = game::confirmation_slots(session,language);
		CHECK(prompt.at(0).text == "/C:" + game::ui_text(game::ui_key::delete_question,language));
		CHECK(prompt.at(2).text == game::ui_text(game::ui_key::yes,language));
		CHECK(game::ui_text(game::ui_key::name_hint,language).find("/O200:") != std::string::npos);
	}
}

TEST_CASE("real data: Menu strings, font encoding and script sound relocation")
{
	auto dir=real_game_dir(); if(!dir) return; auto data=game_data::locate(*dir); REQUIRE(data);
	auto files=load_level_files(*data,"Menu"); auto header=read_level_header(files); scene tree(files.memory,header);
	game::live_memory memory(files.memory); game::interpreter vm(memory,header.main_actor->address());
	game::register_handlers(vm); game::register_dialog_handlers(vm); game::set_dialog_resolver(vm,files);
	game::apply_sound_slots(memory,*data,"Menu");
	// The first confirm and movement nodes identified in node 6's bytecode.
	CHECK(memory.u32(0x7923e7c) == game::menu_confirm_sound);
	// Menu.snd's slots point at sound node parameters, not sound descriptors.
	unsigned confirm_count=0, move_count=0;
	for(const auto& pair:read_snd(*data->resolve("World/Levels/Menu/Menu.snd")))
		for(auto block:files.memory.blocks()) if(block->contains(pair.slot)) {
			const auto reference=files.memory.read_u32({block,pair.slot-block->base});
			if(reference==0x66c03d0) {CHECK(pair.event==game::menu_confirm_sound); ++confirm_count;}
			if(reference==0x66c03f0) {CHECK(pair.event==game::menu_move_sound); ++move_count;}
		}
	CHECK(confirm_count>0); CHECK(move_count>0);
	// Simulate the stock letter writer caching an English label before switching.
	game::remember_text_slot_source(vm,0,game::value::of(game::value_type::id,0x166));
	game::put_text_slot_source_char(vm,0,3,'X');
	for(unsigned language:{1u,2u,3u,4u,0u}) {
		vm.language=std::uint8_t(language);
		auto label=game::resolve_dialog_value(vm,game::value::of(game::value_type::id,0x166)); REQUIRE(label);
		CHECK(label == game::make_dialog_resolver(memory,files,language)(0x166));
	}
	const auto font = game::screen_slot_font_actor(files);
	const auto list = memory.u32(memory.u32(font) + 0x10), entries = memory.u32(list + 4);
	CHECK(memory.u32(entries+12*0x14+4)!=0); // comma used to compose the cedilla
	for(unsigned language=0;language<5;++language) {
		for(std::size_t key=0;key<std::size_t(game::ui_key::count);++key) {
			auto text=game::ui_text(game::ui_key(key),language);
			for(unsigned char byte:text) if(byte>127) {
				INFO("language ",language," key ",key," byte ",unsigned(byte));
				auto glyph=game::screen_slot_glyph_index(byte); REQUIRE(glyph);
				CHECK(memory.u32(entries + unsigned(*glyph)*0x14 + 4) != 0);
			}
		}
	}
	auto french=game::make_dialog_resolver(memory,files,1)(0x5031); REQUIRE(french);
	CHECK(game::screen_slot_glyph_index(0xE7) == game::screen_slot_glyph_index('C'));
}

TEST_CASE("real data: Name Back delivers Esc without saving and main menu has no extra Back")
{
	for(bool escape:{false,true}) {
	auto dir=real_game_dir(); if(!dir) return; auto data=game_data::locate(*dir); REQUIRE(data);
	auto files=load_level_files(*data,"Menu"); auto header=read_level_header(files); scene tree(files.memory,header);
	game::live_memory memory(files.memory); game::interpreter vm(memory,header.main_actor->address());
	game::register_handlers(vm); game::register_dialog_handlers(vm); game::set_dialog_resolver(vm,files);
	auto root=game::save_root()/("back-test-"+std::to_string(std::random_device{}())); vm.save_folder=root.string();
	auto session=std::make_shared<game::save_session>(); game::install_save_handlers(vm,files,header,tree,*dir,session);
	std::vector<std::uint32_t> actors; for(const auto& n:tree.nodes()) if(n.type==super_object_type::actor) actors.push_back(n.at.address());
	vm.set_actors(actors); game::input keys(memory,read_input_actions(files)); auto controller=tree.node(6).at.address();
	game::menu_back_entry back;
	vm.before_think=[&](std::uint32_t actor) {return actor==controller && back.update(vm,keys,controller);};
	bool seen=false;
	for(int tick=0;tick<1100;++tick) {
		keys.set_key(0x1c,tick==240 || tick==600 || (!escape && tick==920));
		keys.set_key(1,escape && tick==920);
		keys.set_key(0xd0,tick==850); keys.step();
		game::step(vm,actors,[](std::uint32_t,const std::exception& e){FAIL(e.what());});
		if(tick==500) CHECK_FALSE(back.active);
		if(tick==800) { REQUIRE(game::menu_name_screen(vm,controller)); CHECK(back.active); seen=true; }
		if(tick==860) {CHECK(back.selected); CHECK(vm.screen_text().at(7).wobble==1);}
	}
	CHECK(seen); CHECK_FALSE(game::menu_name_screen(vm,controller)); CHECK_FALSE(back.active);
	CHECK(session->slot==0); CHECK_FALSE(std::filesystem::exists(root/"Slot1"));
	}
}

TEST_CASE("real data: Load and Delete Back return through the same branch as Esc")
{
	auto dir=real_game_dir(); if(!dir) return; auto data=game_data::locate(*dir); REQUIRE(data);
	for(int entry:{1,2}) {
		std::uint32_t escaped=0;
		for(bool use_back:{false,true}) {
			INFO("entry ",entry," use Back ",use_back);
			auto files=load_level_files(*data,"Menu"); auto header=read_level_header(files); scene tree(files.memory,header);
			game::live_memory memory(files.memory); game::interpreter vm(memory,header.main_actor->address());
			game::register_handlers(vm); game::register_dialog_handlers(vm); game::set_dialog_resolver(vm,files);
			auto root=game::save_root()/("list-back-test-"+std::to_string(std::random_device{}())); vm.save_folder=root.string();
			game::write_save_slot(root,*dir,1,{}, {}, "ONE",0);
			game::write_save_slot(root,*dir,2,{}, {}, "TWO",0);
			auto session=std::make_shared<game::save_session>(); game::install_save_handlers(vm,files,header,tree,*dir,session);
			std::vector<std::uint32_t> actors; for(const auto& n:tree.nodes()) if(n.type==super_object_type::actor) actors.push_back(n.at.address());
			vm.set_actors(actors); game::input keys(memory,read_input_actions(files)); const auto controller=tree.node(6).at.address();
			game::menu_back_entry back;
			vm.before_think=[&](std::uint32_t actor) {return actor==controller && back.update(vm,keys,controller);};
			for(int tick=0;tick<1300;++tick) {
				if(tick==550) {
					REQUIRE(game::script_menu_entry(vm,controller,entry,false));
					game::select_script_menu_entry(vm,controller,entry,false);
					CHECK(vm.get_variable(controller,17).as_int()==entry);
				}
				if(tick==950) {
					const auto mind=memory.u32(memory.u32(memory.u32(controller+4)+0xC));
					INFO("entry ",entry," behaviour ",(memory.u32(memory.u32(mind+4)+8)-memory.u32(memory.u32(memory.u32(mind))))/12," confirmation ",session->confirmation);
					REQUIRE(back.active); if(use_back) REQUIRE(back.select(vm,controller,true));
				}
				keys.set_key(0x1c,tick==240 || tick==600 || (use_back && tick==1000));
				keys.set_key(1,!use_back && tick==1000); keys.step();
				game::step(vm,actors,[](std::uint32_t,const std::exception& e){FAIL(e.what());});
			}
			CHECK_FALSE(back.active); CHECK_FALSE(session->confirmation); CHECK(session->slot==0);
			CHECK(std::filesystem::exists(root/"Slot1/General.sav"));
			const auto mind=memory.u32(memory.u32(memory.u32(controller+4)+0xC));
			const auto behaviour=memory.u32(memory.u32(mind+4)+8);
			if(use_back) CHECK(behaviour==escaped); else escaped=behaviour;
			std::filesystem::remove_all(root);
		}
	}
}
