#include "extruder_panel.h"
#include "i18n.h"
#include "state.h"
#include "config.h"
#include "logger.h"
#include "icons.h"
#include "theme.h"
#include "simple_dialog.h"

using namespace Theme;

#include <algorithm>
#include <cctype>
#include <limits>

namespace {

std::string trim_copy(std::string s) {
  auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c) { return !is_ws(c); }));
  s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c) { return !is_ws(c); }).base(), s.end());
  return s;
}

std::vector<std::string> parse_selector_options(const std::string &csv) {
  std::vector<std::string> options;
  size_t start = 0;
  while (start <= csv.size()) {
    size_t end = csv.find(',', start);
    std::string token = trim_copy(csv.substr(start, end == std::string::npos ? std::string::npos : end - start));
    if (!token.empty()) {
      options.push_back(token);
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return options;
}

std::vector<std::string> load_selector_options(const std::string &config_key, size_t max_options) {
  Config *conf = Config::get_instance();
  auto options = parse_selector_options(conf->get<std::string>(config_key));
  if (options.size() > max_options) {
    LOG_INFO("{} has {} values; truncating to {}", config_key, options.size(), max_options);
    options.resize(max_options);
  }
  return options;
}

std::vector<const char*> build_selector_map(std::vector<std::string> &options) {
  std::vector<const char*> map;
  map.reserve(options.size() + 1);
  for (auto &option : options) {
    map.push_back(option.c_str());
  }
  map.push_back("");
  return map;
}

std::string read_config_value_as_string(const std::string &config_key) {
  Config *conf = Config::get_instance();
  std::string configured_value = trim_copy(conf->get<std::string>(config_key));
  if (!configured_value.empty()) {
    return configured_value;
  }

  int configured_int = conf->get<int>(config_key, std::numeric_limits<int>::min());
  if (configured_int != std::numeric_limits<int>::min()) {
    return std::to_string(configured_int);
  }

  return "";
}

uint32_t resolve_default_idx(const std::string &config_key,
                             const std::vector<std::string> &options) {
  std::string configured_value = read_config_value_as_string(config_key);
  if (!configured_value.empty()) {
    auto it = std::find(options.begin(), options.end(), configured_value);
    if (it != options.end()) {
      return static_cast<uint32_t>(std::distance(options.begin(), it));
    }
  }

  if (options.empty()) {
    return std::numeric_limits<uint32_t>::max();
  }

  return 0;
}

} // namespace

static lv_obj_t *g_tool_row = nullptr;
static int g_tool_count = 0;
static std::string g_active_ext = "extruder";
static std::vector<std::string> g_tool_names;
static std::vector<const char *> g_tool_map;
static std::string g_unload_macro;

static void update_tool_selection(const std::string &name) {
  if (g_tool_row == nullptr) return;

  lv_btnmatrix_clear_btn_ctrl_all(g_tool_row, LV_BTNMATRIX_CTRL_CHECKED);

  int idx = -1;
  if (name == "extruder") {
    idx = 0;
  } else if (name.rfind("extruder", 0) == 0) {
    const std::string suffix = name.substr(8);
    if (!suffix.empty() &&
        std::all_of(suffix.begin(), suffix.end(),
                    [](unsigned char c) { return std::isdigit(c); })) {
      idx = std::atoi(suffix.c_str());
    }
  }

  if (idx >= 0 && idx < g_tool_count) {
    lv_btnmatrix_set_btn_ctrl(g_tool_row, idx, LV_BTNMATRIX_CTRL_CHECKED);
  }
}


ExtruderPanel::ExtruderPanel(KWebSocketClient &websocket_client,
			     std::mutex &lock,
			     Numpad &numpad,
			     SpoolmanPanel &sm)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , panel_cont(create_screen(NULL))
  , spoolman_panel(sm)
  , temp_options(load_selector_options("/ui/extruder_temp_presets", 8))
  , temp_option_map(build_selector_map(temp_options))
  , temp_default_idx(resolve_default_idx("/ui/extruder_temp_default", temp_options))
  , length_options(load_selector_options("/ui/extruder_length_presets", 8))
  , length_option_map(build_selector_map(length_options))
  , length_default_idx(resolve_default_idx("/ui/extruder_length_default", length_options))
  , speed_options(load_selector_options("/ui/extruder_speed_presets", 8))
  , speed_option_map(build_selector_map(speed_options))
  , speed_default_idx(resolve_default_idx("/ui/extruder_speed_default", speed_options))
  , extruder_temp(ws, panel_cont, Icons::EXTRUDER, ::tr("Extruder"), theme_secondary(), false, true, numpad,
                  "extruder", NULL, NULL)
  , tabview(lv_tabview_create(panel_cont, LV_DIR_TOP, scale_r(36)))
  , load_tab(lv_tabview_add_tab(tabview, "Load / Unload"))
  , extrude_tab(lv_tabview_add_tab(tabview, "Extrude / Retract"))
  , load_cont(create_screen(load_tab))
  , extrude_cont(create_screen(extrude_tab))
  , extrude_col(create_row(extrude_cont))
  , load_temp_selector(load_cont, ::tr("Extruder Temperature (C)"),
		       temp_option_map, temp_default_idx, &ExtruderPanel::_handle_callback, this)
  , load_btn(load_cont, Icons::LOAD_FILAMENT_IMG, ::tr("Load"), &ExtruderPanel::_handle_callback, this)
  , unload_btn(load_cont, Icons::UNLOAD_FILAMENT_IMG, ::tr("Unload"), &ExtruderPanel::_handle_callback, this)
  , speed_selector(extrude_cont, ::tr("Extrude Speed (mm/s)"),
		   speed_option_map, speed_default_idx, &ExtruderPanel::_handle_callback, this)
  , length_selector(extrude_cont, ::tr("Extrude Length (mm)"),
		    length_option_map, length_default_idx, &ExtruderPanel::_handle_callback, this)
  , extrude_temp_selector(extrude_cont, ::tr("Extruder Temperature (C)"),
			  temp_option_map, temp_default_idx, &ExtruderPanel::_handle_callback, this)
  , extrude_btn(extrude_col, Icons::EXTRUDE_IMG, ::tr("Extrude"), &ExtruderPanel::_handle_callback, this)
  , retract_btn(extrude_col, Icons::RETRACT_IMG, ::tr("Retract"), &ExtruderPanel::_handle_callback, this)
  , right_col(create_row(panel_cont))
  , spoolman_btn(right_col, Icons::SPOOLMAN_IMG, "Spoolman", &ExtruderPanel::_handle_callback, this)
  , cooldown_btn(right_col, Icons::COOLDOWN_IMG, ::tr("Cooldown"), &ExtruderPanel::_handle_callback, this)
  , back_btn(right_col, Icons::BACK, ::tr("Back"), &ExtruderPanel::_handle_callback, this)
{
  lv_obj_move_background(panel_cont);

  // The readout sits over the tabview, which takes the rest of the height; a
  // column on the right spans both and holds the tiles that apply whichever
  // tab is showing.
  static lv_coord_t grid_panel_col_dsc[] = {LV_GRID_FR(1), (lv_coord_t)scale_w(90), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_panel_row_dsc[] = {LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(panel_cont, grid_panel_col_dsc, grid_panel_row_dsc);
  lv_obj_set_grid_cell(extruder_temp.get_sensor(), LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_START, 0, 1);
  lv_obj_set_grid_cell(tabview, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(right_col, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 2);

  // Genera i pulsanti dagli estrusori dichiarati nel CFG.
  {
    Config *conf = Config::get_instance();
    const auto &configured_sensors = conf->get_objects("/monitored_sensor");

    g_unload_macro.clear();
    int unload_macro_matches = 0;

    for (const auto &macro : conf->get_objects("/macro")) {
      if (macro.value("unload_tool", false)) {
        ++unload_macro_matches;
        g_unload_macro = macro.value("id", std::string());
      }
    }

    if (unload_macro_matches != 1 || g_unload_macro.empty()) {
      if (unload_macro_matches > 1) {
        LOG_ERROR("Multiple macros configured with unload_tool=true; deposit button disabled");
      }
      g_unload_macro.clear();
    }

    std::vector<std::pair<int, std::string>> tools;

    for (const auto &sensor : configured_sensors) {
      if (!sensor.contains("id") || !sensor["id"].is_string()) continue;

      const std::string name = sensor["id"].get<std::string>();
      int idx = -1;

      if (name == "extruder") {
        idx = 0;
      } else if (name.rfind("extruder", 0) == 0) {
        const std::string suffix = name.substr(8);
        if (!suffix.empty() &&
            std::all_of(suffix.begin(), suffix.end(),
                        [](unsigned char c) { return std::isdigit(c); })) {
          idx = std::atoi(suffix.c_str());
        }
      }

      if (idx >= 0 && idx < 8) {
        tools.emplace_back(idx, name);
      }
    }

    std::sort(tools.begin(), tools.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });

    g_tool_names.clear();
    g_tool_map.clear();

    for (const auto &tool : tools) {
      g_tool_names.push_back("T" + std::to_string(tool.first));
    }

    g_tool_count = static_cast<int>(g_tool_names.size());

    if (g_tool_count > 0) {
      for (auto &name : g_tool_names) g_tool_map.push_back(name.c_str());

      if (!g_unload_macro.empty()) {
        g_tool_map.push_back(">Tx");
      }

      g_tool_map.push_back("");

      g_tool_row = lv_btnmatrix_create(panel_cont);
      lv_btnmatrix_set_map(g_tool_row, g_tool_map.data());
      lv_btnmatrix_set_btn_ctrl_all(g_tool_row, LV_BTNMATRIX_CTRL_CHECKABLE);
      lv_btnmatrix_set_one_checked(g_tool_row, true);
      lv_obj_add_style(g_tool_row, &Theme::styles().key_tray, 0);
      lv_obj_set_height(g_tool_row, scale_r(40));

      static lv_coord_t grid_panel_row_tools_dsc[] = {
        LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST
      };
      lv_obj_set_grid_dsc_array(panel_cont, grid_panel_col_dsc, grid_panel_row_tools_dsc);
      lv_obj_set_grid_cell(g_tool_row, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_START, 1, 1);
      lv_obj_set_grid_cell(tabview, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
      lv_obj_set_grid_cell(right_col, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 3);

      lv_obj_add_event_cb(g_tool_row, [](lv_event_t *e) {
        if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

        auto *self = static_cast<ExtruderPanel *>(lv_event_get_user_data(e));
        uint32_t idx = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));

        // Il pulsante >Tx è dopo tutti gli utensili.
        if (idx == static_cast<uint32_t>(g_tool_count)) {
          if (g_unload_macro.empty()) return;

          const std::string active_heater = g_active_ext;

          // Accetta soltanto nomi di estrusori Klipper validi.
          bool valid_heater = active_heater == "extruder";

          if (active_heater.rfind("extruder", 0) == 0 &&
              active_heater != "extruder") {
            const std::string suffix = active_heater.substr(8);
            valid_heater = !suffix.empty() &&
                std::all_of(suffix.begin(), suffix.end(),
                    [](unsigned char c) { return std::isdigit(c); });
          }

          if (!valid_heater) {
            LOG_ERROR("Invalid active extruder '{}'; deposit cancelled", active_heater);
            update_tool_selection(g_active_ext);
            return;
          }

          struct UnloadConfirmData {
            ExtruderPanel *panel;
            std::string macro;
            std::string heater;
          };

          static const char *buttons[] = {::tr("Yes"), ::tr("No"), ""};

          SimpleDialogOptions options{};
          options.buttons = buttons;
          options.auto_close = true;
          options.highlighted_button_idx = 0;
          options.user_data = new UnloadConfirmData{
            self, g_unload_macro, active_heater
          };

          options.result_cb = [](lv_obj_t *, uint32_t button_idx, void *user_data) {
            auto *data = static_cast<UnloadConfirmData *>(user_data);
            if (data == nullptr) return;

            if (button_idx == 0) {
              const std::string command = fmt::format(
                  "{}\nSET_HEATER_TEMPERATURE HEATER={} TARGET=0",
                  data->macro, data->heater);
              data->panel->ws.gcode_script(command);
            } else {
              update_tool_selection(g_active_ext);
            }

            delete data;
          };

          std::string message = ::tr("Deposit selected tool?");
          create_configurable_dialog(lv_scr_act(), ::tr("Select tool"),
                                     message.c_str(), options);
          return;
        }

        if (idx >= static_cast<uint32_t>(g_tool_count)) return;

        struct ToolConfirmData {
          ExtruderPanel *panel;
          uint32_t index;
        };

        static const char *buttons[] = {::tr("Yes"), ::tr("No"), ""};
        SimpleDialogOptions options{};
        options.buttons = buttons;
        options.auto_close = true;
        options.highlighted_button_idx = 0;
        options.user_data = new ToolConfirmData{self, idx};
        options.result_cb = [](lv_obj_t *, uint32_t button_idx, void *user_data) {
          auto *data = static_cast<ToolConfirmData *>(user_data);
          if (data == nullptr) return;

          if (button_idx == 0) {
            data->panel->ws.gcode_script(fmt::format("T{}", data->index));
          } else {
            update_tool_selection(g_active_ext);
          }

          delete data;
        };

        std::string message = fmt::format(::tr("Select tool T{}?"), idx);
        create_configurable_dialog(lv_scr_act(), ::tr("Select tool"),
                                   message.c_str(), options);
      }, LV_EVENT_VALUE_CHANGED, this);
    }
  }

  for (lv_obj_t *tab : {load_tab, extrude_tab}) {
    lv_obj_set_style_pad_all(tab, 0, 0);
  }
  style_embedded_tabview(tabview);

  ButtonContainer *all_tiles[] = {&spoolman_btn, &load_btn, &unload_btn, &extrude_btn, &retract_btn,
                                  &cooldown_btn, &back_btn};
  for (ButtonContainer *b : all_tiles) b->use_plain();

  // Spoolman, Cooldown and Back divide the right-hand column evenly.
  lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(right_col, 0, 0);
  for (ButtonContainer *b : {&spoolman_btn, &cooldown_btn, &back_btn}) {
    lv_obj_set_width(b->get_container(), LV_PCT(100));
    lv_obj_set_height(b->get_container(), 0);
    lv_obj_set_flex_grow(b->get_container(), 1);
  }

  // Load / Unload: the pair side by side, the temperature presets beneath.
  static lv_coord_t grid_load_row_dsc[] = {LV_GRID_FR(6), LV_GRID_FR(6), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_load_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(load_cont, grid_load_col_dsc, grid_load_row_dsc);
  lv_obj_set_grid_cell(load_btn.get_container(), LV_GRID_ALIGN_STRETCH, 0, 1,
                       LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_grid_cell(unload_btn.get_container(), LV_GRID_ALIGN_STRETCH, 1, 1,
                       LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_grid_cell(load_temp_selector.get_container(), LV_GRID_ALIGN_STRETCH, 0, 2,
                       LV_GRID_ALIGN_STRETCH, 1, 1);

  // Extrude / Retract: the pair stacked on the left, three selectors beside them.
  static lv_coord_t grid_extrude_row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_extrude_col_dsc[] = {LV_GRID_FR(3), LV_GRID_FR(7), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(extrude_cont, grid_extrude_col_dsc, grid_extrude_row_dsc);
  lv_obj_set_grid_cell(extrude_col, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 3);
  lv_obj_set_flex_flow(extrude_col, LV_FLEX_FLOW_COLUMN);
  for (ButtonContainer *b : {&extrude_btn, &retract_btn}) {
    lv_obj_set_width(b->get_container(), LV_PCT(100));
    lv_obj_set_height(b->get_container(), 0);
    lv_obj_set_flex_grow(b->get_container(), 1);
  }
  Selector *mid[] = {&speed_selector, &length_selector, &extrude_temp_selector};
  for (int r = 0; r < 3; r++) {
    lv_obj_set_grid_cell(mid[r]->get_container(), LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, r, 1);
    // Three selectors share the column, so a cell is shorter than a full key
    // row plus the panel's padding: let the keys shrink into what it leaves.
    lv_obj_set_style_min_height(mid[r]->get_selector(), 0, 0);
    lv_obj_set_style_pad_bottom(mid[r]->get_container(), 0, 0);
  }

  spoolman_btn.disable();  // state, not style: until moonraker says spoolman is there
  spoolman_btn.hide();

  ws.register_notify_update(this);
}

ExtruderPanel::~ExtruderPanel() {
  if (panel_cont != NULL) {
    lv_obj_del(panel_cont);
    panel_cont = NULL;
  }
}

void ExtruderPanel::foreground() {
  lv_tabview_set_act(tabview, lv_obj_get_index(load_tab), LV_ANIM_OFF);
  lv_obj_move_foreground(panel_cont);
}

void ExtruderPanel::background() {
  lv_obj_move_background(panel_cont);
}

void ExtruderPanel::enable_spoolman() {
  spoolman_btn.enable();
}

void ExtruderPanel::show_spoolman(bool shown) {
  if (shown) lv_obj_clear_flag(spoolman_btn.get_container(), LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(spoolman_btn.get_container(), LV_OBJ_FLAG_HIDDEN);
}

void ExtruderPanel::init_active_tool(json &j) {
  json &active_ext = j["/result/status/toolhead/extruder"_json_pointer];
  if (!active_ext.is_string()) return;

  std::string name = active_ext.get<std::string>();
  g_active_ext = name;
  extruder_temp.set_object(name, name.c_str());

  json &st = j[json::json_pointer("/result/status/" + name)];
  if (st.is_object()) {
    if (st.contains("target") && st["target"].is_number())
      extruder_temp.update_target(st["target"].get<int>());
    if (st.contains("temperature") && st["temperature"].is_number())
      extruder_temp.update_value(st["temperature"].get<int>());
  }

  update_tool_selection(name);
}

void ExtruderPanel::consume(json& j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  auto target_value = j[json::json_pointer("/params/0/" + g_active_ext + "/target")];
  if (!target_value.is_null()) {
    int target = target_value.template get<int>();
    extruder_temp.update_target(target);
  }

  auto temp_value = j[json::json_pointer("/params/0/" + g_active_ext + "/temperature")];
  if (!temp_value.is_null()) {
    int value = temp_value.template get<int>();
    extruder_temp.update_value(value);
  }

  json &active_ext = j["/params/0/toolhead/extruder"_json_pointer];
  if (active_ext.is_string()) {
    std::string name = active_ext.template get<std::string>();
    if (name != g_active_ext) {
      // il tool attivo e' cambiato: nome, temperatura e target seguono il nuovo riscaldatore
      g_active_ext = name;
      extruder_temp.set_object(name, name.c_str());
      json &st = State::get_instance()->get_data(json::json_pointer("/printer_state/" + name));
      if (st.is_object()) {
        if (st.contains("target") && st["target"].is_number()) extruder_temp.update_target(st["target"].template get<int>());
        if (st.contains("temperature") && st["temperature"].is_number()) extruder_temp.update_value(st["temperature"].template get<int>());
      }
    }
    if (g_tool_row != nullptr) {
      update_tool_selection(name);
    }
  }

  json &pstat_state = j["/params/0/print_stats/state"_json_pointer];
  if (!pstat_state.is_null()) {
    if (pstat_state.template get<std::string>() == "printing") {
      lv_obj_move_background(panel_cont);
    }
  }
}

void ExtruderPanel::handle_callback(lv_event_t *e) {
  LOG_TRACE("handling extruder panel callback");
  if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t *selector = lv_event_get_target(e);
    uint32_t idx = lv_btnmatrix_get_selected_btn(selector);
    const char * v = lv_btnmatrix_get_btn_text(selector, idx);

    for (Selector *sel : {&load_temp_selector, &speed_selector, &length_selector,
                          &extrude_temp_selector}) {
      if (selector == sel->get_selector()) {
        sel->set_selected_idx(idx);
      }
    }

    LOG_TRACE("selector {} {} {}", fmt::ptr(selector), idx, v);
    
  } else if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(e);

    if (btn == back_btn.get_container()) {
      lv_obj_move_background(panel_cont);
    }

    Config *conf = Config::get_instance();
    auto selected = [](Selector &sel) {
      return lv_btnmatrix_get_btn_text(sel.get_selector(), sel.get_selected_idx());
    };

    if (btn == extrude_btn.get_container()) {
      if (!extrude_btn.start_pressed_transition(2000)) {
        return;
      }
      const std::string extrude_macro = conf->get<std::string>("/default_macros/extrude");
      ws.gcode_script(fmt::format(extrude_macro, selected(extrude_temp_selector),
                                  selected(length_selector), std::stoi(selected(speed_selector)) * 60));
    }

    if (btn == retract_btn.get_container()) {
      if (!retract_btn.start_pressed_transition(2000)) {
        return;
      }
      const std::string retract_macro = conf->get<std::string>("/default_macros/retract");
      ws.gcode_script(fmt::format(retract_macro, selected(extrude_temp_selector),
                                  selected(length_selector), std::stoi(selected(speed_selector)) * 60));
    }

    if (btn == unload_btn.get_container()) {
      if (!unload_btn.start_pressed_transition(2000)) {
        return;
      }
      const std::string unload_filament_macro = conf->get<std::string>("/default_macros/unload_filament");

      ws.gcode_script(fmt::format(unload_filament_macro, selected(load_temp_selector)));
    }

    if (btn == load_btn.get_container()) {
      if (!load_btn.start_pressed_transition(2000)) {
        return;
      }
      const std::string load_filament_macro = conf->get<std::string>("/default_macros/load_filament");

      ws.gcode_script(fmt::format(load_filament_macro, selected(load_temp_selector)));
    }

    // disable() clears the clickable flag, so a tap cannot arrive while it is off
    if (btn == spoolman_btn.get_container()) {
      spoolman_panel.foreground();
    }

    if (btn == cooldown_btn.get_container()) {
      if (!cooldown_btn.start_pressed_transition(1000)) {
        return;
      }
      const std::string cooldown_macro = conf->get<std::string>("/default_macros/cooldown");
      ws.gcode_script(cooldown_macro);
    }
  }
}
