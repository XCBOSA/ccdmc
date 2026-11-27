#include "inventory_ui.h"
#include "../game/armor.h"
#include "../game/brewing.h"
#include "../game/item_icons.h"
#include "../game/item_custom.h"
#include "../game/game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *icon_layers[]={"Icon0","Icon1","Icon2","Icon3"};
/* Same logical pixel scale on both axes, centered in the square cell.
   Counts are drawn afterwards over the lower right, without moving the art. */
static double unit(char digit) {
    return (digit>='A' ? digit-'A'+10 : digit-'0')*2.;
}
static AutoTree path_commands(const char *form,double scale) {
    AutoTree path=tree_newarr();
    if (!form) return path;
    int started=0;
    for (const char *p=form;*p;) {
        if (*p==';') {
            if (started) tree_array_append(path,mc_close());
            started=0; ++p; continue;
        }
        if (!p[1]) break;
        double x=((SLOT_EDGE-30)*.5+unit(p[0]))*scale,y=((SLOT_EDGE-30)*.5+unit(p[1]))*scale;
        tree_array_append(path,started ? mc_line_to(x,y) : mc_move(x,y));
        started=1; p+=2;
    }
    if (started) tree_array_append(path,mc_close());
    return path;
}

static void key(char id[32], const char *prefix, int index, const char *part) {
    snprintf(id, 32, "%s%d%s", prefix, index, part);
}

static void icon_color(AutoTree shape,const ItemIcon *icon,int layer,int has_icon) {
    char hex[8];
    snprintf(hex,sizeof hex,"#%06X",icon->rgb[layer]);
    shape.fill=hex;
    shape.opacity=has_icon && icon->path[layer] && *icon->path[layer] ? 1 : 0;
}
static const char *slot_fill(const char *prefix,ItemStack item,int selected) {
    return selected ? "#777E6E" : item.enchants ? "#5D456F" :
        !strcmp(prefix,"bagSlot") ? "#838383" : "#454545";
}
static void slot_hint(char *hint,size_t size,const char *prefix,int index,int selected,ItemStack item) {
    const ItemDef *def=item.count ? item_def(item.type) : NULL;
    const char *name=def ? item_display_name(item) : "空格";
    const unsigned remaining=def && item.damage<def->durability ? def->durability-item.damage : 0;
    if (!strcmp(prefix,"brewSlot")) {
        if (def) snprintf(hint,size,"%s槽 · %s × %u",brew_slot_name(index),name,(unsigned)item.count);
        else snprintf(hint,size,"%s槽 · 空格",brew_slot_name(index));
    } else if (!strcmp(prefix,"armorSlot")) {
        if (def) snprintf(hint,size,"%s · %s · 耐久 %u/%u",armor_slot_name(index),name,remaining,(unsigned)def->durability);
        else snprintf(hint,size,"%s · 未装备",armor_slot_name(index));
    } else if (!strcmp(prefix,"recipeSlot")) {
        snprintf(hint,size,"合成格 %d · %s",index+1,name);
    } else if (!strcmp(prefix,"recipeOutput")) {
        snprintf(hint,size,"合成结果 · %s",name);
    } else {
        const char *group=!strcmp(prefix,"bagSlot") ? "背包" : "";
        const char *focus=selected ? " · 已选中" : "";
        if (def) snprintf(hint,size,"%s第 %d 格 · %s × %u%s",group,index+1,name,(unsigned)item.count,focus);
        else snprintf(hint,size,"%s第 %d 格 · 空格%s",group,index+1,focus);
    }
    if (def && is_axe(item.type)) {
        size_t end=strlen(hint);
        if (strcmp(prefix,"recipeSlot") && strcmp(prefix,"recipeOutput"))
            snprintf(hint+end,size-end," · 耐久 %u/%u",remaining,(unsigned)def->durability);
    }
}
static AutoTree make_slot(const char *prefix,int index,CCDMCAction tap,int type,int preview,
    const ItemStack *initial,const char *accessibility,int edge,int selected) {
    double scale=(double)edge/SLOT_EDGE;
    char id[32];
    AutoTree shapes = tree_newarr();
    AutoTree tile = mc_rect(scale, scale, edge - 2*scale, edge - 2*scale);
    key(id, prefix, index, "Frame"); tile.id = id;
    tile.fill = initial ? slot_fill(prefix,*initial,selected) : "#777777";
    tile.stroke = selected ? "#FFFFFF" : initial ? (initial->enchants ? "#C084F2" : "#3B3B3B") : "#363636";
    tile.lineWidth = (selected ? 3 : 2)*scale;
    tree_array_append(shapes, tile);
    ItemIcon icon;
    int has_icon=item_icon(type,&icon);
    for (int i=0;i<ITEM_ICON_LAYERS;++i) {
        AutoTree shape=mc_path(path_commands(has_icon ? icon.path[i] : NULL,scale));
        key(id,prefix,index,icon_layers[i]); shape.id=id;
        icon_color(shape,&icon,i,has_icon);
        tree_array_append(shapes, shape);
    }
    if (!preview) {
        const ItemDef *def=initial && initial->damage ? item_def(initial->type) : NULL;
        int show_wear=def && initial->damage<def->durability;
        AutoTree wear = mc_rect(5*scale,36*scale,
            show_wear ? 34*scale*(def->durability-initial->damage)/def->durability : 0,3*scale);
        key(id, prefix, index, "Wear"); wear.id = id;
        wear.fill = "#72D95A"; wear.opacity = show_wear ? 1 : 0;
        tree_array_append(shapes, wear);
        char value[12]="";
        if (initial && initial->count>1) snprintf(value,sizeof value,"%u",(unsigned)initial->count);
        AutoTree shadow = mc_text(24*scale, 27*scale, value);
        key(id, prefix, index, "Shadow"); shadow.id = id; shadow.color = "#151515"; shadow.fontSize = 13*scale;
        tree_array_append(shapes, shadow);
        AutoTree count = mc_text(23*scale, 26*scale, value);
        key(id, prefix, index, "Count"); count.id = id; count.color = "#FFFFFF"; count.fontSize = 13*scale;
        tree_array_append(shapes, count);
    }
    AutoTree cell = mc_canvas(shapes, edge, edge);
    key(id, prefix, index, ""); cell.id = id;
    cell.iconType=type;
    if (accessibility) cell.accessibilityLabel=accessibility;
    else if (has_icon) {
        char title[ITEM_NAME_BYTES+32];
        snprintf(title,sizeof title,"取用%s",item_name(type));
        cell.accessibilityLabel=title;
    }
    if (tap) {
        AutoTree gestures=tree_newarr();
        AutoTree gesture=mc_tap(1,tap);
        key(id,prefix,index,"Tap"); gesture.id=id;
        tree_array_append(gestures,gesture);
        cell.gestures=gestures;
    }
    return cell;
}
AutoTree make_item_slot(const char *prefix,int index,CCDMCAction tap) {
    if (!strcmp(prefix,"brewSlot")) {
        char hint[48];
        slot_hint(hint,sizeof hint,prefix,index,0,(ItemStack){0});
        return make_slot(prefix,index,tap,AIR,0,NULL,hint,SLOT_EDGE,0);
    }
    return make_slot(prefix,index,tap,AIR,0,NULL,NULL,SLOT_EDGE,0);
}
AutoTree make_storage_slot(int index,CCDMCAction tap,ItemStack item,const char *accessibility,int edge,int selected) {
    return make_slot("storage_",index,tap,item.count ? item.type : AIR,0,&item,accessibility,edge,selected);
}
static AutoTree make_runtime_slot(const char *prefix,int index,CCDMCAction tap,int edge,int selected) {
    ItemStack item=(unsigned)index<SLOT_COUNT ? inventory[index] : (ItemStack){0};
    char hint[ITEM_NAME_BYTES+72];
    slot_hint(hint,sizeof hint,prefix,index,selected,item);
    return make_slot(prefix,index,tap,item.count ? item.type : AIR,0,&item,hint,edge,selected);
}
AutoTree make_inventory_slot(int index,CCDMCAction tap,int edge,int selected) {
    return make_runtime_slot("bagSlot",index,tap,edge,selected);
}
AutoTree make_hotbar_slot(int index,CCDMCAction tap,int edge,int selected) {
    return make_runtime_slot("hotSlot",index,tap,edge,selected);
}
AutoTree make_item_preview_slot(const char *prefix,int type,CCDMCAction tap,int edge) {
    return make_slot(prefix,type,tap,type,1,NULL,NULL,edge,0);
}

static void paint(AutoTree root,const char *prefix,int index,int highlighted,ItemStack item) {
    char id[32],value[12],hint[ITEM_NAME_BYTES+72];
    key(id,prefix,index,"");
    AutoTree slot=ccd_ui_id(root,id);
    double scale=(double)slot.width/SLOT_EDGE;
    if (!(scale>0 && scale<=1)) scale=1;
    key(id, prefix, index, "Frame");
    AutoTree frame = ccd_ui_id(root, id);
    frame.stroke = highlighted ? "#FFFFFF" : item.enchants ? "#C084F2" : "#3B3B3B";
    frame.lineWidth = (highlighted ? 3 : 2)*scale;
    frame.fill = slot_fill(prefix,item,highlighted);
    const ItemDef *def = item.count ? item_def(item.type) : NULL;
    int type=def ? item.type : AIR;
    if ((int)slot.iconType!=type) {
        ItemIcon icon;
        int has_icon=item_icon(type,&icon);
        for (int i=0;i<ITEM_ICON_LAYERS;++i) {
            key(id,prefix,index,icon_layers[i]);
            AutoTree shape=ccd_ui_id(root,id);
            shape.sub=path_commands(has_icon ? icon.path[i] : NULL,scale);
            icon_color(shape,&icon,i,has_icon);
        }
        slot.iconType=type;
    }
    key(id, prefix, index, "Wear");
    AutoTree wear = ccd_ui_id(root, id);
    int show_wear=def && def->durability && item.damage && item.damage<def->durability;
    wear.width = show_wear ? 34*scale*(def->durability-item.damage)/def->durability : 0;
    wear.opacity = show_wear ? 1 : 0;
    snprintf(value, sizeof value, "%u", (unsigned)item.count);
    key(id, prefix, index, "Count"); ccd_ui_id(root, id).text = item.count > 1 ? value : "";
    key(id, prefix, index, "Shadow"); ccd_ui_id(root, id).text = item.count > 1 ? value : "";
    slot_hint(hint,sizeof hint,prefix,index,highlighted,item);
    slot.accessibilityLabel=hint;
}

void paint_item_slot(AutoTree root, const char *prefix, int index, int highlighted) {
    if (index < 0 || index >= SLOT_COUNT) return;
    paint(root,prefix,index,highlighted,inventory[index]);
}
void paint_item_stack(AutoTree root, const char *prefix, int index, ItemStack item) {
    paint(root,prefix,index,0,item);
}
void paint_selected_stack(AutoTree root,const char *prefix,int index,int selected,ItemStack item) {
    paint(root,prefix,index,selected,item);
}

int item_slot_index(AutoTree sender, const char *prefix) {
    const char *id = (const char *)sender.id;
    size_t len = strlen(prefix);
    if (!id || strncmp(id, prefix, len) || id[len] < '0' || id[len] > '9') return -1;
    char *end;
    long index = strtol(id + len, &end, 10);
    int limit = !strcmp(prefix, "palette") ? ITEM_LIMIT : SLOT_COUNT;
    if ((*end && strcmp(end, "Tap")) || index < 0 || index >= limit) return -1;
    return (int)index;
}
