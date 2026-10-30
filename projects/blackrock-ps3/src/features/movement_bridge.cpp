 
#include "features/movement_bridge.h"

#include "ui/widgets.h"

namespace brps3::mvbridge {

namespace {

ItemInfo infoOf(const ui::RegistryItem& it) {
    ItemInfo i;
    i.id = it.id;
    i.label = it.label;
    i.help = it.help;
    i.page = it.page;
    i.toggle = it.kind == ui::RegistryItem::Kind::Switch;
    i.dangerous = it.dangerous;
    i.icon = it.icon;
    i.where = it.where;
    return i;
}

}   

std::vector<ItemInfo> bindableItems() {
    std::vector<ItemInfo> out;
    for (const ui::RegistryItem& it : ui::registryItems()) {
        if (it.kind != ui::RegistryItem::Kind::Switch && it.kind != ui::RegistryItem::Kind::Action) continue;
        if (it.portCheck) continue;                     
        if (it.id == "movement.binds") continue;        
        if (it.dangerous) continue;                     
        out.push_back(infoOf(it));
    }
    return out;
}

std::optional<ItemInfo> item(const std::string& id) {
    ui::RegistryItem it;
    if (!ui::findRegistryItem(id, &it)) return std::nullopt;
    return infoOf(it);
}

bool activate(const std::string& id) { return ui::activateItem(id); }

bool toggleOn(const std::string& id) { return ui::itemSwitchOn(id); }

float value(const std::string& id, float fallback) { return ui::itemValue(id, fallback); }

bool applyValue(const std::string& id, float v) { return ui::applyItemValue(id, v); }

void reveal(const std::string& id) { ui::revealItem(id); }

void demoToggle(const std::string& id, bool on) { ui::demoShowSwitch(id, on); }

}   
