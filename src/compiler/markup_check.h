#ifndef ZAN_MARKUP_CHECK_H
#define ZAN_MARKUP_CHECK_H

/* S20（docs/ZAN_STANDARD.md §5）：窗体标记参与编译——只检查、不生成代码。
 *
 * 用户源文件里派生自 Gui.Form / Gui.ChildWindow 的类 X，配对同目录的 X.html
 * （或 --embed 进来的同名 .html），按运行期装配同一口径（DocWire）在编译期查：
 *   E2112  @事件 / v-on: / z-on: / data-on- / on* 处理器名不是 X（含基类）的方法
 *   E2113  bind / bind:属性 / :属性 的模型路径不是 X 的字段/属性（逐段按声明类型下钻）
 *   E2114  z-mount / data-mount 挂载的字段不存在
 *   E2115  bind / :属性 的值不是点分模型路径（表达式、字面量）或属性名不是标识符
 *          ——运行期建树时 DeclareBinding 对这些直接抛异常
 * 诊断落在 .html 里的属性值上，拼写最近的唯一成员给 fix-it。运行期这些错误
 * 是开窗前 exit 1；编译期报出来，AI 拿到的是错误位置而不是一个空白窗口。
 * 有意不报（避免误报）：模板行（<template> 子树）的 bind 路径与挂载（路径语法仍查）、非标识符的
 * 处理器值（路由动作 "open-x"）、在 X 的源码里以字符串字面量登记过的处理器名
 * （binder.Handle("Name", ...)）、X 自己声明了 ViewModel() 时的 bind 路径。 */

#include "ast.h"
#include "binder.h"
#include "diag.h"

/* explicit_input_count：前多少个文件编号是用户显式输入（只查这些文件里的类）。
 * 返回报出的错误条数。 */
int zan_markup_check(zan_ast_node_t *unit, zan_binder_t *binder, zan_diag_t *diag,
                     int explicit_input_count, const char *const *embed_specs,
                     int embed_spec_count);

#endif /* ZAN_MARKUP_CHECK_H */
