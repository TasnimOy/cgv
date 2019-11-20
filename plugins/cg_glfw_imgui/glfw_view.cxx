#include "glfw_view.h"
#include <cgv/render/drawable.h>
#include <cgv/base/register.h>
#include <cgv/utils/ostream_printf.h>
#include <cgv/utils/scan.h>
#include <cgv/type/variant.h>
#include <cgv/gui/key_event.h>
#include <cgv/gui/mouse_event.h>
#include <cgv_gl/gl/gl.h>
#include <iostream>


void glfw_view::configure_opengl_controls()
{
	if (find_control(debug))
		find_control(debug)->set("active", version >= 30);
	if (find_control(forward_compatible))
		find_control(forward_compatible)->set("active", version >= 30);
	if (find_control(core_profile))
		find_control(core_profile)->set("active", version >= 32);
}

void glfw_view::process_text_1(const std::string& text)
{
	process_text(text);
}

/// construct application
glfw_view::glfw_view(int x, int y, int w, int h, const std::string& name) 
	: group(name)
{
	version = -1;

	in_draw_method = false;
	redraw_request = false;

	enabled = false;

	//dnd_release_event_queued = false;

	connect(out_stream.write, this, &glfw_view::process_text_1);
	//mode(determine_mode());
}

///
bool glfw_view::self_reflect(cgv::reflect::reflection_handler& srh)
{
	return
		cgv::render::context_config::self_reflect(srh) &&
		srh.reflect_member("version", version) &&
		srh.reflect_member("enable_vsynch", enable_vsynch) &&
		srh.reflect_member("sRGB_framebuffer", sRGB_framebuffer) &&
		srh.reflect_member("show_help", show_help) &&
		srh.reflect_member("show_stats", show_stats) &&
		srh.reflect_member("font_size", info_font_size) &&
		srh.reflect_member("tab_size", tab_size) &&
		srh.reflect_member("performance_monitoring", enabled) &&
		srh.reflect_member("bg_r", bg_r) &&
		srh.reflect_member("bg_g", bg_g) &&
		srh.reflect_member("bg_b", bg_b) &&
		srh.reflect_member("bg_a", bg_a) &&
		srh.reflect_member("bg_index", current_background) &&
		srh.reflect_member("gamma", gamma) &&
		srh.reflect_member("nr_display_cycles", nr_display_cycles) &&
		srh.reflect_member("bar_line_width", bar_line_width) &&
		srh.reflect_member("file_name", file_name);
}

void glfw_view::on_set(void* member_ptr)
{
	if (member_ptr == &version) {
		if (version == -1)
			version_major = version_minor = -1;
		else {
			version_major = version / 10;
			version_minor = version % 10;
		}
		member_ptr = &version_major;
	}
	if (member_ptr >= static_cast<context_config*>(this) && member_ptr < static_cast<context_config*>(this) + 1) {
		int new_mode = determine_mode();
		if (is_created() && !in_recreate_context) { // && new_mode != mode()) {
			if (can_do(new_mode))
				change_mode(new_mode);
			else
				synch_with_mode();
		}
	}
	if (member_ptr == &current_background) {
		set_bg_clr_idx(current_background);
		update_member(&bg_r);
	}
	update_member(member_ptr);
	if (member_ptr == &bg_g || member_ptr == &bg_b)
		update_member(&bg_r);
	if (member_ptr == &bg_accum_g || member_ptr == &bg_accum_b)
		update_member(&bg_accum_r);
	post_redraw();
}

/// returns the property declaration
std::string glfw_view::get_property_declarations()
{
	return fltk_base::get_property_declarations()+";"+cgv::base::group::get_property_declarations()+";stencil_buffer:bool;accum_buffer:bool;quad_buffer:bool;multisample:bool";
}

void glfw_view::change_mode(int m)
{
	in_recreate_context = true;
	if (mode() == m)
		mode_ = -1;
	fltk_driver::set_context_creation_attrib_list(*this);
	if (!mode(m))
		std::cerr << "could not change mode to " << m << std::endl;
	current_font_face.clear();
	current_font_size = 0;
	synch_with_mode();
	in_recreate_context = false;
	update_member(this);
}

int glfw_view::determine_mode()
{
	int m = fltk::NO_AUTO_SWAP;
	if (depth_buffer)
		m = m | fltk::DEPTH_BUFFER;
	if (stereo_buffer)
		m = m | fltk::STEREO;
	if (stencil_buffer)
		m = m | fltk::STENCIL_BUFFER;
	if (alpha_buffer)
		m = m | fltk::ALPHA_BUFFER;
	if (accumulation_buffer)
		m = m | fltk::ACCUM_BUFFER;
	if (context_config::double_buffer)
		m = m | fltk::DOUBLE_BUFFER;
	if (multi_sample_buffer)
		m = m | fltk::MULTISAMPLE;
	return m;
}

/// set the context_config members from the current fltk mode
void glfw_view::synch_with_mode()
{
	version = 10 * version_major + version_minor;

	update_member(&version);
	update_member(&version_major);
	update_member(&version_minor);
	update_member(&debug);
	update_member(&core_profile);
	update_member(&forward_compatible);
	configure_opengl_controls();

	int m = mode();
	make_current();
	bool new_stereo_buffer = (m & fltk::STEREO) != 0;
	if (new_stereo_buffer != stereo_buffer) {
		stereo_buffer = new_stereo_buffer;
		update_member(&stereo_buffer);
	}
	bool new_depth_buffer = (m & fltk::DEPTH_BUFFER) != 0;
	if (new_depth_buffer != depth_buffer) {
		depth_buffer = new_depth_buffer;
		update_member(&depth_buffer);
	}
	if (depth_buffer) {
		GLint depthSize = 24;
		if (version >= 30) {
			glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,
				GL_DEPTH, GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &depthSize);
		}
		depth_bits = depthSize;
	}
	else
		depth_bits = -1;
	update_member(&depth_bits);
	
	bool new_stencil_buffer = (m & fltk::STENCIL_BUFFER) != 0;
	if (new_stencil_buffer != stencil_buffer) {
		stencil_buffer = new_stencil_buffer;
		update_member(&stencil_buffer);
	}
	if (stencil_buffer) {
		GLint stencilSize = 8;
		if (version >= 30) {
			glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,
				GL_STENCIL, GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE, &stencilSize);
		}
		stencil_bits = stencilSize;
	}
	else
		stencil_bits = -1;
	update_member(&stencil_bits);
	
	bool new_alpha_buffer = (m & fltk::ALPHA_BUFFER) != 0;
	if (new_alpha_buffer != alpha_buffer) {
		alpha_buffer = new_alpha_buffer;
		update_member(&alpha_buffer);
	}
	bool new_multi_sample_buffer = (m & fltk::MULTISAMPLE) != 0;
	if (new_multi_sample_buffer != multi_sample_buffer) {
		multi_sample_buffer = new_multi_sample_buffer;
		update_member(&multi_sample_buffer);
	}
	if (multi_sample_buffer) {
		GLint nrSamples = 0;
		if (version >= 13)
			glGetIntegerv(GL_SAMPLES, &nrSamples);
		nr_multi_samples = nrSamples;
	}
	else
		nr_multi_samples = -1;
	update_member(&nr_multi_samples);
	
	bool new_double_buffer = (m & fltk::DOUBLE_BUFFER) != 0;
	if (new_double_buffer != context_config::double_buffer) {
		context_config::double_buffer = new_double_buffer;
		update_member(&(context_config::double_buffer));
	}
	bool new_accumulation_buffer = (m & fltk::ACCUM_BUFFER) != 0;
	if (new_accumulation_buffer != accumulation_buffer) {
		accumulation_buffer = new_accumulation_buffer;
		update_member(&accumulation_buffer);
	}
}

/// attach or detach (\c attach=false) an alpha buffer to the current frame buffer if not present
void glfw_view::attach_alpha_buffer(bool attach)
{
	if (alpha_buffer != attach) {
		alpha_buffer = attach;
		on_set(&alpha_buffer);
	}
}
/// attach or detach (\c attach=false) depth buffer to the current frame buffer if not present
void glfw_view::attach_depth_buffer(bool attach)
{
	if (depth_buffer != attach) {
		depth_buffer = attach;
		on_set(&depth_buffer);
	}
}
/// attach or detach (\c attach=false) stencil buffer to the current frame buffer if not present
void glfw_view::attach_stencil_buffer(bool attach)
{
	if (stencil_buffer != attach) {
		stencil_buffer = attach;
		on_set(&stencil_buffer);
	}
}

/// return whether the graphics card supports stereo buffer mode
bool glfw_view::is_stereo_buffer_supported() const
{
	return can_do(mode() | fltk::STEREO);
}
/// attach or detach (\c attach=false) stereo buffer to the current frame buffer if not present
void glfw_view::attach_stereo_buffer(bool attach)
{
	if (stereo_buffer != attach) {
		stereo_buffer = attach;
		on_set(&stereo_buffer);
	}
}
/// attach or detach (\c attach=false) accumulation buffer to the current frame buffer if not present
void glfw_view::attach_accumulation_buffer(bool attach)
{
	if (accumulation_buffer != attach) {
		accumulation_buffer = attach;
		on_set(&accumulation_buffer);
	}
}
/// attach or detach (\c attach=false) multi sample buffer to the current frame buffer if not present
void glfw_view::attach_multi_sample_buffer(bool attach)
{
	if (multi_sample_buffer != attach) {
		multi_sample_buffer = attach;
		on_set(&multi_sample_buffer);
	}
}

/// attach a stencil buffer to the current frame buffer if not present
bool glfw_view::recreate_context()
{
	int m = determine_mode();
	if (!can_do(m))
		return false;

	change_mode(m);
	return true;
}

#include <cgv/gui/dialog.h>

/// abstract interface for the setter 
bool glfw_view::set_void(const std::string& property, const std::string& value_type, const void* value_ptr)
{
	if (fltk_base::set_void(this,this,property, value_type, value_ptr))
		return true;
	return base::set_void(property, value_type, value_ptr);
}

/// abstract interface for the getter 
bool glfw_view::get_void(const std::string& property, const std::string& value_type, void* value_ptr)
{
	if (fltk_base::get_void(this,this,property, value_type, value_ptr))
		return true;
	return base::get_void(property, value_type, value_ptr);
}

/// interface of a handler for traverse callbacks
class debug_traverse_callback_handler : public traverse_callback_handler
{
protected:
	int i;
	void tab() { for (int j=0; j<i; ++j) std::cout << " "; }
	std::string get_name(base_ptr b) const {
		named_ptr n = b->get_named();
		if (n && !n->get_name().empty())
			return n->get_name();
		return b->get_type_name();
	}
public:
	debug_traverse_callback_handler() : i(0) {}
	/// called before a node b is processed, return, whether to skip this node. If the node is skipped, the on_leave_node callback is still called
	bool on_enter_node(base_ptr b) { tab(); std::cout << "enter node " << get_name(b) << std::endl; ++i; return false; }
	/// called when a node b is left, return whether to terminate traversal
	bool on_leave_node(base_ptr b) { --i; tab(); std::cout << "leave node " << get_name(b) << std::endl; return false; }
	/// called before the children of a group node g are processed, return whether these should be skipped. If children are skipped, the on_leave_children callback is still called.
	bool on_enter_children(group_ptr g) { tab(); std::cout << "enter children " << get_name(g) << std::endl; ++i; return false; }
	/// called when the children of a group node g have been left, return whether to terminate traversal
	virtual bool on_leave_children(group_ptr g) { --i; tab(); std::cout << "leave children " << get_name(g) << std::endl; return false; }
	/// called before the parent of a node n is processed, return whether this should be skipped. If the parent is skipped, the on_leave_parent callback is still called.
	virtual bool on_enter_parent(node_ptr n) { tab(); std::cout << "enter parent " << get_name(n) << std::endl; ++i; return false; } 
	/// called when the parent of a node n has been left, return whether to terminate traversal
	virtual bool on_leave_parent(node_ptr n) { --i; tab(); std::cout << "leave parent " << get_name(n) << std::endl; return false; }
};

#include <fltk/../../compat/FL/Fl.H>

void glfw_view::create()
{
	glfw_driver::set_context_creation_attrib_list(*this);
	fltk::GlWindow::create();
	make_current();
	last_context = get_context(this);
	if (!configure_gl()) {
		abort();
	}
	synch_with_mode();
}

void glfw_view::destroy()
{
	make_current();
	if (!in_recreate_context) {
		remove_all_children();
		no_more_context = true;
	}
	else {
		single_method_action<cgv::render::drawable,void,cgv::render::context&> sma(*this, &drawable::clear, true, true);
		for (unsigned i=0; i<get_nr_children(); ++i)
			traverser(sma, "nc").traverse(get_child(i));
	}
	destruct_render_objects();
	fltk::GlWindow::destroy();
}

/// helper method to remove a child
void glfw_view::clear_child(base_ptr child)
{
	single_method_action<cgv::render::drawable,void,cgv::render::context&> sma(*this, &drawable::clear, false, false);
	traverser(sma,"nc").traverse(child);

	on_remove_child(child);
}

/// append child and return index of appended child
unsigned int glfw_view::append_child(base_ptr child)
{
	if (child->get_interface<drawable>() == 0)
		return -1;
	unsigned int i = group::append_child(child);
	configure_new_child(child);
	set_focused_child(i);
	return i;
}
/// remove all elements of the vector that point to child, return the number of removed children
unsigned int glfw_view::remove_child(base_ptr child)
{
	clear_child(child);
	unsigned j = group::remove_child(child);
	int i = get_focused_child();
	if (i != -1) {
		if (j <= (unsigned)i) {
			if (i == 0)
				i = get_nr_children();
			set_focused_child(i-1);
		}
	}
	return j;
}
/// remove all children
void glfw_view::remove_all_children()
{
	for (unsigned int i=0; i<get_nr_children(); ++i)
		clear_child(get_child(i));
	group::remove_all_children();
}
/// insert a child at the given position
void glfw_view::insert_child(unsigned int i, base_ptr child)
{
	if (child->get_interface<drawable>() == 0)
		return;
	group::insert_child(i,child);
	configure_new_child(child);
	set_focused_child(i);
}

///
void glfw_view::idle_callback(void* ptr)
{
	glfw_view* gl_view = (glfw_view*) ptr;
	gl_view->redraw();
}

/// overload render pass to perform measurements
void glfw_view::render_pass(cgv::render::RenderPass rp, cgv::render::RenderPassFlags rpf, void* ud)
{
	start_task((int)rp);
	cgv::render::gl::gl_context::render_pass(rp, rpf, ud);
	if (enabled)
		glFinish();
	else
		glFlush();
	finish_task((int)rp);
}

/// complete drawing method that configures opengl whenever the context has changed, initializes the current frame and draws the scene
void glfw_view::draw()
{
	if (!started_frame_pm)
		start_frame();

	in_draw_method = true;
	bool last_redraw_request = redraw_request;
	redraw_request = false;
	if (!valid()) {
		if (get_context(this) != last_context) {
			last_width = get_width();
			last_height = get_height();
			configure_gl();
			synch_with_mode();
		}
		else if ((int)last_width != get_width() || (int)last_height != get_height()) {
			last_width = get_width();
			last_height = get_height();
			resize_gl();
		}
	}
	render_pass(RP_MAIN, default_render_flags);

	if (enabled) {
		cgv::render::gl::gl_performance_monitor::draw(*this);	
		glFlush();
	}

	swap_buffers();

	if (enabled) {
		finish_frame();
		update_member(&fps);
		if (redraw_request) {
			start_frame();
			started_frame_pm = true;
		}
		else 
			started_frame_pm = false;
	}
	if (instant_redraw)
		redraw_request = true;

	in_draw_method = false;
	if (redraw_request != last_redraw_request) {
		if (redraw_request)
			fltk::add_idle(idle_callback, this);
		else
			fltk::remove_idle(idle_callback, this);
	}
}

/// put the event focus on the given child 
bool glfw_view::set_focus(base_ptr child)
{
	for (unsigned int i = 0; i < get_nr_children(); ++i)
		if (child == get_child(i)) {
			set_focused_child(i);
			if (show_stats)
				redraw();
			return true;
		}
	return false;
}

/// return the currently focused child or an empty base ptr if no child has focus
cgv::base::base_ptr glfw_view::get_focus() const
{
	int i = get_focused_child();
	if (i == -1)
		return base_ptr();
	return get_child(i);
}

/// handle events emitted by event sources
bool glfw_view::handle(event& e)
{
	if (e.get_kind() == EID_KEY) {
		key_event& ke = static_cast<key_event&>(e);
		if (ke.get_action() == KA_PRESS) {
			switch (ke.get_key()) {
			case KEY_F4 : 
				if (ke.get_modifiers() == EM_ALT) {
					cgv::base::unregister_all_objects();
					destroy();
#ifdef WIN32
					TerminateProcess(GetCurrentProcess(),0);
#else
					exit(0);
#endif
				}
				break;
			case KEY_F1 : 
				if (ke.get_modifiers() == 0) {				
					show_help = !show_help;
					redraw();
					return true;
				}
				break;
			case KEY_Delete :
				if (ke.get_modifiers() == 0 && get_nr_children() > 1 && get_focused_child() != -1) {
					cgv::base::unregister_object(get_child(get_focused_child()), "");
//						remove_child(get_child(get_focused_child()));
					if (get_focused_child() >= (int)get_nr_children())
						traverse_policy::set_focused_child(get_focused_child()-1);
					redraw();
					return true;
				}
				break;
			case KEY_Tab :
				if (ke.get_modifiers() == EM_SHIFT) {
					traverse_policy::set_focused_child(get_focused_child()-1);
					if (get_focused_child() == -2)
						traverse_policy::set_focused_child((int)get_nr_children()-1);
					if (show_stats)
						redraw();
					return true;
				}
				else if (ke.get_modifiers() == 0) {
					traverse_policy::set_focused_child(get_focused_child()+1);
					if (get_focused_child() >= (int)get_nr_children())
						traverse_policy::set_focused_child(-1);
					if (show_stats)
						redraw();
					return true;
				}
				break;
			case KEY_F8 : 
				if (ke.get_modifiers() == 0) {
					show_stats = !show_stats; 
					redraw();
					return true;
				}
				if (ke.get_modifiers() == EM_CTRL) {
					enabled = !enabled; 
					redraw();
					return true;
				}
				break;
			case KEY_F10 :
				if (ke.get_modifiers() == 0) {
					do_screen_shot = true;
					redraw();
					return true;
				}
				break;
			case KEY_F12 :
				if (ke.get_modifiers() == 0) {
					set_bg_clr_idx(current_background+1);
					return true;
				}
				break;
			default: 
				return false;
			}
		}
	}
	return false;
}
/// overload to stream help information to the given output stream
void glfw_view::stream_help(std::ostream& os)
{
	os << "MAIN: Show   ... help: F1;                  ... [perf-]status: [Ctrl-]F8;\n";
	os << "      Object ... change focus: [Shift-]Tab; ... remove focused: Delete\n";
	os << "      Screen ... copy to file: F10;         ... background: F12\n";
	os << "      Toggle ... menu: Menu; GUI: Shift-Key; FullScreen: F11; Monitor: Shift-F11\n";
	os << "Quit: Alt-F4\n" << std::endl;
}

void glfw_view::stream_stats(std::ostream& os)
{
	std::string name("no focus");
	if (get_focused_child() != -1) {
		base_ptr c = get_child(get_focused_child());
		named_ptr n = c->get_interface<named>();
		if (!n.empty()) {
			name  = n->get_name();
			name += ':';
		}
		else
			name = "";
		name += c->get_type_name();
	}
	oprintf(os, "WxH=%d:%d focus = %s<Tab>\n", w(), h(), name.c_str());
}

/// return the width of the window
unsigned int glfw_view::get_width() const
{
	return w();
}
/// return the height of the window
unsigned int glfw_view::get_height() const
{
	return h();
}

/// resize the context to the given dimensions
void glfw_view::resize(unsigned int width, unsigned int height)
{
	node_ptr p = get_parent();
	if (p) {
		p->set("W", width);
		p->set("H", height);
	}
	else {
		fltk::GlWindow::resize(width, height);
//		cgv::render::gl::gl_context::resize(width, height);
	}
}

/// return whether the context is currently in process of rendering
bool glfw_view::in_render_process() const
{
	return in_draw_method;
}

/// return whether the context is created
bool glfw_view::is_created() const
{
	return get_context(this) != 0;
}

/// return whether the context is current
bool glfw_view::is_current() const
{
	return false;
}
/// make the current context current
bool glfw_view::make_current() const
{
	if (no_more_context)
		return false;
	const_cast<fltk::GlWindow*>(static_cast<const fltk::GlWindow*>(this))->make_current();
	return true;
}

/// clear current context lock
void glfw_view::clear_current() const
{
#if USE_X11
	std::cerr << "clear_current() not implemented for FLTK under linux!" << std::endl;
	// glXMakeCurrent(xdisplay, 0, 0);
#elif defined(_WIN32)
	wglMakeCurrent(0, 0);
#elif defined(__APPLE__)
	// warning: the Quartz version should probably use Core GL (CGL) instead of AGL
	aglSetCurrentContext(0);
#endif
}

/// the context will be redrawn when the system is idle again
void glfw_view::post_redraw()
{
	if (in_render_process())
		redraw_request = true;
//		std::cerr << "redraw does not work in render process" << std::endl;
	else
		redraw();
}


void glfw_view::force_redraw()
{
	if (in_render_process()) {
		std::cerr << "force_redraw not allowed in render process" << std::endl;
		post_redraw();
		return;
	}
	flush();
	set_damage(0);
}

/// select a font given by a font handle
void glfw_view::enable_font_face(font_face_ptr font_face, float font_size)
{
	
	//if (!(font_face == current_font_face) || font_size != current_font_size) {
		fltk_font_face_ptr fff = font_face.up_cast<fltk_font_face>();
		if (fff.empty()) {
			std::cerr << "could not use font face together with fltk" << std::endl;
			return;
		}
		if (!core_profile)
			fltk::glsetfont(fff->get_fltk_font(),font_size);
		current_font_face = font_face;
		current_font_size = font_size;
	//}
}

void glfw_view::draw_text(const std::string& text)
{
	if (text.empty())
		return;
	glRasterPos2i(cursor_x,cursor_y);
	GLint r_prev[4];
	glGetIntegerv(GL_CURRENT_RASTER_POSITION, r_prev);
	fltk::gldrawtext(text.c_str(), (int)text.size());
	GLint r[4];
	glGetIntegerv(GL_CURRENT_RASTER_POSITION, r);
	cursor_x += r[0]-r_prev[0];
	cursor_y -= r[1]-r_prev[1];
}

bool glfw_view::dispatch_event(const event& e)
{
	// FIXME: cast from (const event&) to (event&) is dirty
	single_method_action<event_handler,bool,event&> sma((event&)e,&event_handler::handle);
	return traverser(sma).traverse(group_ptr(this));
}

/// process focus and key press and release events here
int glfw_view::handle(int ei)
{
	fltk_base::handle(this,ei);
	int dx = fltk::event_x() - last_mouse_x;
	int dy = fltk::event_y() - last_mouse_y;
	last_mouse_x = fltk::event_x();
	last_mouse_y = fltk::event_y();

	switch (ei) {
	case fltk::FOCUS:
	case fltk::UNFOCUS: 
		return 1;
	case fltk::KEY :
		if (dispatch_event(cgv_key_event(fltk::event_key_repeated() ? KA_REPEAT : KA_PRESS)))
			return 1;
		if (fltk::event_key() >= fltk::HomeKey &&
			fltk::event_key() <= fltk::EndKey)
			return 1;
		break;
	case fltk::KEYUP :
		if (dispatch_event(cgv_key_event(KA_RELEASE)))
			return 1;
		break;
	case fltk::PUSH:
		if (dispatch_event(cgv_mouse_event(MA_PRESS)))
			return 1;
		break;
	case fltk::RELEASE:
		if (dispatch_event(cgv_mouse_event(MA_RELEASE)))
			return 1;
		break;
	case fltk::MOUSEWHEEL:
		if (dispatch_event(cgv_mouse_event(MA_WHEEL)))
			return 1;
		break;
	case fltk::MOVE:
		if (!dnd_release_event_queued) {
			if ( (dx != 0 || dy != 0) && dispatch_event(cgv_mouse_event(MA_MOVE,dx,dy)))
				return 1;
		}
		else 
			return 1;
		break;
	case fltk::DRAG:
		if (dx != 0 || dy != 0) {
			if (dispatch_event(cgv_mouse_event(MA_DRAG,dx,dy)))
				return 1;
		}
		else
			return 1;
		break;
	case fltk::ENTER:
		take_focus();
		dispatch_event(cgv_mouse_event(MA_ENTER));
		return 1;
	case fltk::LEAVE:
		if (dispatch_event(cgv_mouse_event(MA_LEAVE)))
			return 1;
		break;
	case fltk::DND_ENTER:
		if (dispatch_event(cgv_mouse_event(MA_ENTER, EF_DND)))
			return 1;
		break;
	case fltk::DND_DRAG :
		if (dx != 0 || dy != 0) {
			if (dispatch_event(cgv_mouse_event(MA_DRAG,EF_DND,dx,dy)))
				return 1;
		}
		else
			return 1;
		break;
	case fltk::DND_LEAVE:
		if (dispatch_event(cgv_mouse_event(MA_LEAVE, EF_DND)))
			return 1;
		break;
	case fltk::DND_RELEASE :
		dnd_release_event = cgv_mouse_event(MA_RELEASE, EF_DND);
		dnd_release_event_queued = true;
		return 1;
	case fltk::PASTE :
		dnd_release_event_queued = false;
		dnd_release_event.set_dnd_text(fltk::event_text());
		if (dispatch_event(dnd_release_event))
			return 1;
		break;
	}
	return fltk::GlWindow::handle(ei);       // hand on event to base class
}

void* to_void_ptr(int i) {
	void* res = 0;
	(int&) res = i;
	return res;
}
/// enable phong shading with the help of a shader (enabled by default)
void glfw_view::enable_phong_shading()
{
	cgv::render::context::enable_phong_shading();
	update_member(&phong_shading);
}
/// disable phong shading
void glfw_view::disable_phong_shading()
{
	gl_context::disable_phong_shading();
	update_member(&phong_shading);
}
void glfw_view::configure_opengl_controls()
{
	if (find_control(debug))
		find_control(debug)->set("active", version >= 30);
	if (find_control(forward_compatible))
		find_control(forward_compatible)->set("active", version >= 30);
	if (find_control(core_profile))
		find_control(core_profile)->set("active", version >= 32);
}

///  
void glfw_view::create_gui()
{
	add_decorator("gl view", "heading");
	if (begin_tree_node("rendering", fps, false, "level=3")) {
		provider::align("\a");
		add_view("fps", fps, "", "w=72", " ");
		add_member_control(this, "EWMA", fps_alpha, "value_slider", "min=0;max=1;ticks=true;w=120;align='B';tooltip='coefficient of exponentially weighted moving average'");
		add_member_control(this, "vsynch", enable_vsynch, "toggle", "w=92", " ");
		add_member_control(this, "instant redraw", instant_redraw, "toggle", "w=100");
		add_member_control(this, "gamma", gamma, "value_slider", "min=0.2;max=5;ticks=true;log=true;tooltip='default gamma used for inverse gamma correction of fragment color'");
		add_member_control(this, "sRGB_framebuffer", sRGB_framebuffer, "check");
		provider::align("\b");
		end_tree_node(fps);
	}
	if (begin_tree_node("debug", enabled, false, "level=3")) {
		provider::align("\a");
		add_member_control(this, "show_help", show_help, "check");
		add_member_control(this, "show_stats", show_stats, "check");
		add_member_control(this, "debug_render_passes", debug_render_passes, "check");
		add_member_control(this, "performance monitoring", enabled, "check");
		add_member_control(this, "time scale", time_scale, "value_slider", "min=1;max=90;ticks=true;log=true");
		add_gui("placement", placement, "", "options='min=0;max=500'");
		provider::align("\b");
		end_tree_node(enabled);
	}

	if (begin_tree_node("buffers", stereo_buffer, false, "level=3")) {
		provider::align("\a");
		add_member_control(this, "alpha buffer", alpha_buffer, "toggle");
		add_member_control(this, "double buffer", context_config::double_buffer, "toggle");

		add_view("depth bits", depth_bits, "", "w=32", " ");
		add_member_control(this, "depth buffer", depth_buffer, "toggle", "w=160");
		
		add_view("stencil bits", stencil_bits, "", "w=32", " ");
		add_member_control(this, "stencil buffer", stencil_buffer, "toggle", "w=160");

		add_view("accumulation bits", accumulation_bits, "", "w=32", " ");
		add_member_control(this, "accumulation buffer", accumulation_buffer, "toggle", "w=160");

		add_view("nr multi samples", nr_multi_samples, "", "w=32", " ");
		add_member_control(this, "multi sample buffer", multi_sample_buffer, "toggle", "w=160");

		add_member_control(this, "stereo buffer", stereo_buffer, "toggle");
		provider::align("\b");
		end_tree_node(stereo_buffer);
	}
	if (begin_tree_node("opengl", version, false, "level=3")) {
		provider::align("\a");
		add_member_control(this, "version", reinterpret_cast<cgv::type::DummyEnum&>(version), "dropdown", 
			"w=120;enums='detect=-1,1.0=10,1.1=11,1.2=12,1.3=13,1.4=14,1.5=15,2.0=20,2.1=21,3.0=30,3.1=31,3.2=32,3.3=33,4.0=40,4.1=41,4.2=42,4.3=43,4.4=44,4.5=45,4.6=46'", " ");
		add_view("", version_major, "", "w=32", " ");
		add_view(".", version_minor, "", "w=32");
		add_member_control(this, "core", core_profile, "toggle", "w=60", " ");
		add_member_control(this, "fwd_comp", forward_compatible, "toggle", "w=60", " ");
		add_member_control(this, "debug", debug, "toggle", "w=60");
		configure_opengl_controls();
		provider::align("\b");
		end_tree_node(version);
	}
	if (begin_tree_node("clear", bg_r, false, "level=3")) {
		provider::align("\a");
		add_member_control(this, "color", (cgv::media::color<float>&) bg_r);
		add_member_control(this, "alpha", bg_a, "value_slider", "min=0;max=1;ticks=true;step=0.001");
		add_member_control(this, "accum color", (cgv::media::color<float>&) bg_accum_r);
		add_member_control(this, "accum alpha", bg_accum_a, "value_slider", "min=0;max=1;ticks=true;step=0.001");
		add_member_control(this, "stencil", bg_s, "value_slider", "min=0;max=1;ticks=true;step=0.001");
		add_member_control(this, "depth", bg_d, "value_slider", "min=0;max=1;ticks=true;step=0.001");
		provider::align("\b");
		end_tree_node(bg_r);
	}
	if (begin_tree_node("compatibility", support_compatibility_mode, false, "level=3")) {
		provider::align("\a");
		add_member_control(this, "auto_set_view_in_current_shader_program", auto_set_view_in_current_shader_program, "check");
		add_member_control(this, "auto_set_lights_in_current_shader_program", auto_set_lights_in_current_shader_program, "check");
		add_member_control(this, "auto_set_material_in_current_shader_program", auto_set_material_in_current_shader_program, "check");
		add_member_control(this, "support_compatibility_mode", support_compatibility_mode, "check");
		add_member_control(this, "draw_in_compatibility_mode", draw_in_compatibility_mode, "check");
		provider::align("\b");
		end_tree_node(support_compatibility_mode);
	}
	if (begin_tree_node("defaults", current_material_is_textured, false, "level=3")) {
		provider::align("\a");
		if (begin_tree_node("default_render_flags", default_render_flags, false, "level=4")) {
			add_gui("default_render_flags", default_render_flags, "bit_field_control",
			"enums='RPF_SET_PROJECTION = 1,RPF_SET_MODELVIEW = 2,RPF_SET_LIGHTS = 4,RPF_SET_MATERIAL = 8,\
RPF_SET_LIGHTS_ON=16,RPF_ENABLE_MATERIAL=32,RPF_CLEAR_COLOR=64,RPF_CLEAR_DEPTH=128,\
RPF_CLEAR_STENCIL=256,RPF_CLEAR_ACCUM=512,\
RPF_DRAWABLES_INIT_FRAME=1024,RPF_SET_STATE_FLAGS=2048,\
RPF_SET_CLEAR_COLOR=4096,RPF_SET_CLEAR_DEPTH=8192,RPF_SET_CLEAR_STENCIL=16384,RPF_SET_CLEAR_ACCUM=32768,\
RPF_DRAWABLES_DRAW=65536,RPF_DRAWABLES_FINISH_FRAME=131072,\
RPF_DRAW_TEXTUAL_INFO=262144,RPF_DRAWABLES_AFTER_FINISH=524288,RPF_HANDLE_SCREEN_SHOT=1048576");
			end_tree_node(default_render_flags);
		}


		if (begin_tree_node("material", default_material, false, "level=4")) {
			provider::align("\a");
			add_gui("material", default_material);
			provider::align("\b");
			end_tree_node(default_material);
		}
		for (int i = 0; i < nr_default_light_sources; ++i) {
			if (begin_tree_node("light[" + cgv::utils::to_string(i) + "]", default_light_source[i], false, "level=4")) {
				provider::align("\a");
				add_gui("light", default_light_source[i]);
				provider::align("\b");
				end_tree_node(default_light_source[i]);
			}
		}
		provider::align("\b");
		end_tree_node(current_material_is_textured);
	}
}
