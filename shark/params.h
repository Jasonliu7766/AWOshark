#pragma once
// 网页调参用的参数描述（单独放头文件，Arduino 自动生成函数声明时才认得这个类型）
struct Param { const char *key, *name; float *v; float def, mn, mx, step; const char *group, *label; };
