if(NOT DEFINED ROOT)
  message(FATAL_ERROR "ROOT is required")
endif()
file(TO_CMAKE_PATH "${ROOT}" ROOT)

# Legacy bootstrap files and Html.Standard must stay deleted.
foreach(legacy IN ITEMS
    "${ROOT}/packages/Zan.Gui/src/Gui/Core/Control/ControlBootstrap.zan"
    "${ROOT}/packages/Zan.Gui/src/Gui/Markup/Html/Html.Standard.zan")
  if(EXISTS "${legacy}")
    message(FATAL_ERROR "Legacy bootstrap artifact still exists: ${legacy}")
  endif()
endforeach()

set(factory "${ROOT}/packages/Zan.Gui/src/Gui/Core/Control/ControlFactory.zan")
if(NOT EXISTS "${factory}")
  message(FATAL_ERROR "control factory is missing: ${factory}")
endif()
file(READ "${factory}" factory_source)

# ControlFactory must declare compiler-synthesized [GuiKindManifest] hooks
# and must never inline `return new ...` branches.
foreach(mode IN ITEMS "has" "kinds" "create" "createLower")
  if(NOT factory_source MATCHES "\\[GuiKindManifest\\(\"${mode}\"\\)\\]")
    message(FATAL_ERROR "ControlFactory.zan missing [GuiKindManifest(\"${mode}\")] extern")
  endif()
endforeach()
string(REGEX MATCH "return[ ]+new[ ]+" inlined "${factory_source}")
if(inlined)
  message(FATAL_ERROR
    "ControlFactory inlines a return-new branch; declare [GuiKind] on controls instead")
endif()

# Control.Props.zan must provide the default Type.Name-based Kind().
set(control_props "${ROOT}/packages/Zan.Gui/src/Gui/Core/Control/Control.Props.zan")
if(NOT EXISTS "${control_props}")
  message(FATAL_ERROR "Control.Props.zan is missing: ${control_props}")
endif()
file(READ "${control_props}" control_props_source)
if(NOT control_props_source MATCHES "virtual[ \t]+string[ \t]+Kind[ \t]*\\([ \t]*\\)")
  message(FATAL_ERROR "Control.Props.zan missing default `virtual string Kind()`")
endif()

# Verify [GuiKind("...")] uniqueness and Kind() consistency across packages.
file(GLOB_RECURSE zan_files LIST_DIRECTORIES false "${ROOT}/packages/*.zan")
set(seen_kinds "")
set(checked_count 0)
foreach(path IN LISTS zan_files)
  file(READ "${path}" raw_text)
  string(REGEX REPLACE "//[^\n]*" "" text "${raw_text}")
  string(REGEX MATCHALL "\\[GuiKind(Attribute)?\\(\"([A-Za-z0-9_]+)\"[^\\]]*\\][ \t\r\n]+[^\r\n{]+" matches "${text}")
  if(NOT matches)
    continue()
  endif()
  foreach(m IN LISTS matches)
    string(REGEX REPLACE "\\[GuiKind(Attribute)?\\(\"([A-Za-z0-9_]+)\".*" "\\2" kind "${m}")
    list(FIND seen_kinds "${kind}" duplicate)
    if(NOT duplicate EQUAL -1)
      message(FATAL_ERROR "Duplicate [GuiKind(\"${kind}\")] across packages (also in ${path})")
    endif()
    list(APPEND seen_kinds "${kind}")
    math(EXPR checked_count "${checked_count} + 1")

    if(NOT kind STREQUAL "Element")
      # Either the class name itself matches kind (inheriting Control.Kind()),
      # it is a static factory alias (e.g. CreateChart / CreateRadioButton),
      # or the file provides an explicit `override string Kind()`.
      string(REGEX MATCH "(^|[ \t\r\n])class[ \t]+${kind}(<[A-Za-z0-9_, \t]*>)?[ \t]*[:{]" has_matching_class "${text}")
      string(REGEX MATCH "static[ \t]+Control[ \t]+Create[A-Za-z0-9_]*[ \t]*\\(" is_static_alias "${m}")
      string(REGEX MATCH "override[ \t]+string[ \t]+Kind[ \t]*\\([ \t]*\\)" has_kind_override "${text}")
      if(NOT has_matching_class AND NOT is_static_alias AND NOT has_kind_override)
        message(FATAL_ERROR
          "[GuiKind(\"${kind}\")] in ${path} lacks matching class `${kind}` or `override string Kind()`")
      endif()
    endif()
  endforeach()
endforeach()

if(checked_count LESS 80)
  message(FATAL_ERROR "Expected at least 80 [GuiKind] entries across packages, found ${checked_count}")
endif()

message(STATUS
  "control factory policy OK: ${checked_count} unique [GuiKind] entries, Control.Kind() default, and [GuiKindManifest] hooks")
