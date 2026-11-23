if(NOT DEFINED ROOT)
  message(FATAL_ERROR "ROOT is required")
endif()
file(TO_CMAKE_PATH "${ROOT}" ROOT)
# 按名构造的策略闸：ControlFactory 只做查表转发，不得手写控件名单或内联
# 控件构造分支。每个分支里的类名都是活标识符，按需拉取会把全部控件拖进
# 编译图；名单要么来自编译器合成的控件目录（docs/GUI_CONTROL_CATALOG.md），
# 要么来自运行期 Register。目录本身的正确性由 GUI 用例 control_catalog /
# control_factory_equivalence 覆盖，这里只守 ControlFactory 源码形态。
set(factory "${ROOT}/packages/Zan.Gui/src/Gui/Core/Control/ControlFactory.zan")
if(NOT EXISTS "${factory}")
  message(FATAL_ERROR "control factory is missing: ${factory}")
endif()
file(READ "${factory}" source)

if(NOT source MATCHES "public[ \t]+class[ \t]+ControlFactory[ \t]*[:{]")
  message(FATAL_ERROR "${factory} no longer declares class ControlFactory")
endif()

# 手写名单：List<string> 初始化器里出现字符串字面量。
string(REGEX MATCH "new[ \t]+List<string>[ \t]*(\\(\\))?[ \t]*\\{[^}]*\"" literal_list "${source}")
if(literal_list)
  message(FATAL_ERROR
    "ControlFactory hard-codes a kind list; kinds come from the control catalog or Register")
endif()

# 内联构造：return new <类>，集合容器（List/Dict）的空实例除外。
string(REGEX MATCHALL "return[ \t]+new[ \t]+[A-Za-z0-9_]+" returns "${source}")
foreach(ret IN LISTS returns)
  string(REGEX REPLACE "^return[ \t]+new[ \t]+" "" cls "${ret}")
  if(NOT cls STREQUAL "List" AND NOT cls STREQUAL "Dict")
    message(FATAL_ERROR
      "ControlFactory inlines a construction branch (return new ${cls}); "
      "let the control catalog or ControlFactory.Register construct it")
  endif()
endforeach()

# 字符串分派：kind == "X" 形态的分支同样把控件名写死在工厂里。
string(REGEX MATCH "kind[ \t]*==[ \t]*\"[A-Za-z0-9_]+\"" kind_branch "${source}")
if(kind_branch)
  message(FATAL_ERROR
    "ControlFactory dispatches on a literal kind (${kind_branch}); "
    "kinds come from the control catalog or Register")
endif()

message(STATUS "control factory policy OK: ControlFactory only forwards (no literal kinds or inline constructors)")
