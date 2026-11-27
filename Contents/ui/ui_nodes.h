#ifndef CCDMC_UI_NODES_H
#define CCDMC_UI_NODES_H

/* Use the documented CCDUI node schema for the game-specific helpers.
   Existing Engine releases expose the node protocol but do not all
   supply the same convenience constructors in ccduicomp.h. */
typedef void (*CCDMCAction)(AutoTree,AutoTree);
static inline AutoTree mc_canvas(AutoTree children,double w,double h) {
    return @{type:"canvas",sub:children,width:w,height:h};
}
static inline AutoTree mc_timer(double interval,int repeats,CCDMCAction action) {
    return @{type:"timer",interval:interval,repeats:repeats,action:action};
}
static inline AutoTree mc_rect(double x,double y,double w,double h) {
    return @{type:"drawRect",x:x,y:y,width:w,height:h};
}
static inline AutoTree mc_ellipse(double x,double y,double w,double h) {
    return @{type:"drawEllipse",x:x,y:y,width:w,height:h};
}
static inline AutoTree mc_line(double x,double y,double x2,double y2) {
    return @{type:"drawLine",x:x,y:y,x2:x2,y2:y2};
}
static inline AutoTree mc_path(AutoTree commands) {
    return @{type:"drawPath",sub:commands};
}
static inline AutoTree mc_text(double x,double y,const char *value) {
    return @{type:"drawText",x:x,y:y,text:value};
}
static inline AutoTree mc_move(double x,double y) {
    return @{type:"move",x:x,y:y};
}
static inline AutoTree mc_line_to(double x,double y) {
    return @{type:"lineTo",x:x,y:y};
}
static inline AutoTree mc_close(void) {
    return @{type:"close"};
}
static inline AutoTree mc_tap(int count,CCDMCAction action) {
    return @{type:"tapGesture",count:count,action:action};
}
static inline AutoTree mc_press(double duration,CCDMCAction action) {
    return @{type:"longPressGesture",duration:duration,action:action};
}
static inline AutoTree mc_drag(double distance,CCDMCAction action) {
    return @{type:"dragGesture",minimumDistance:distance,continuous:1,action:action};
}
static inline AutoTree mc_safe(AutoTree children) {
    return @{type:"safeArea",sub:children};
}
static inline AutoTree mc_geometry(AutoTree children,CCDMCAction action) {
    return @{type:"geometryReader",sub:children,action:action};
}
static inline AutoTree mc_color(const char *value) {
    return @{type:"color",value:value};
}

#endif
