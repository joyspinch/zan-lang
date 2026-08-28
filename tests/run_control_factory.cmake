if(NOT DEFINED ROOT)
  message(FATAL_ERROR "ROOT is required")
endif()
file(TO_CMAKE_PATH "${ROOT}" ROOT)
set(factory "${ROOT}/stdlib/Gui/ControlFactory.zan")
if(NOT EXISTS "${factory}")
  message(FATAL_ERROR "control factory is missing: ${factory}")
endif()
file(READ "${factory}" source)

string(REGEX MATCH "new List<string>[ \t]*\\{([^}]*)\\}" init "${source}")
if(NOT init)
  message(FATAL_ERROR "ControlFactory.Kinds() has no literal built-in list")
endif()
set(kind_body "${CMAKE_MATCH_1}")
string(REGEX MATCHALL "\"[A-Za-z0-9_]+\"" kind_tokens "${kind_body}")
set(kinds "")
foreach(token IN LISTS kind_tokens)
  string(REGEX REPLACE "^\"|\"$" "" kind "${token}")
  list(APPEND kinds "${kind}")
endforeach()

set(create_kinds "")
set(create_classes "")
# A generic control is constructed with explicit type arguments
# (`new DataGrid<string>()`), so the constructor pattern allows an
# optional <...> between the class name and the call parentheses.
string(REGEX MATCHALL
  "kind[ \t]*==[ \t]*\"[A-Za-z0-9_]+\"[^\n]*return[ \t]+new[ \t]+[A-Za-z0-9_]+(<[A-Za-z0-9_, \t]*>)?[ \t]*\\("
  branches "${source}")
foreach(branch IN LISTS branches)
  string(REGEX REPLACE
    ".*kind[ \t]*==[ \t]*\"([A-Za-z0-9_]+)\".*return[ \t]+new[ \t]+([A-Za-z0-9_]+)(<[A-Za-z0-9_, \t]*>)?[ \t]*\\(.*"
    "\\1" branch_kind "${branch}")
  string(REGEX REPLACE
    ".*kind[ \t]*==[ \t]*\"[A-Za-z0-9_]+\".*return[ \t]+new[ \t]+([A-Za-z0-9_]+)(<[A-Za-z0-9_, \t]*>)?[ \t]*\\(.*"
    "\\1" branch_class "${branch}")
  list(APPEND create_kinds "${branch_kind}")
  list(APPEND create_classes "${branch_class}")
endforeach()

set(sorted_kinds "${kinds}")
set(sorted_create "${create_kinds}")
list(REMOVE_DUPLICATES sorted_kinds)
list(REMOVE_DUPLICATES sorted_create)
list(SORT sorted_kinds)
list(SORT sorted_create)
if(NOT "${sorted_kinds}" STREQUAL "${sorted_create}")
  message(FATAL_ERROR
    "ControlFactory.Kinds()/Create() mismatch: Kinds() and Create() constructor sets differ")
endif()

file(GLOB_RECURSE zan_files LIST_DIRECTORIES false "${ROOT}/stdlib/Gui/*.zan")
set(kind_labels "")
list(LENGTH create_classes class_count)
math(EXPR last_class "${class_count} - 1")
if(last_class GREATER_EQUAL 0)
  foreach(i RANGE 0 ${last_class})
    list(GET create_classes ${i} class)
    set(class_source "")
    foreach(path IN LISTS zan_files)
      file(READ "${path}" text)
      string(REGEX MATCH
        "class[ \t]+${class}(<[A-Za-z0-9_, \t]*>)?[ \t]*[:{]" class_match "${text}")
      if(class_match)
        # Anchor on the full regex match (e.g. "class Marquee :"), not a bare
        # "class ${class}" FIND: a sibling class whose name extends this one
        # ("class MarqueeAnim" in the same file) otherwise hijacks the slice
        # and the Kind() check inspects the wrong body.
        string(FIND "${text}" "${class_match}" class_pos)
        string(SUBSTRING "${text}" ${class_pos} -1 class_source)
        string(FIND "${class_source}" "\nclass " next_class)
        if(next_class GREATER 0)
          string(SUBSTRING "${class_source}" 0 ${next_class} class_source)
        endif()
        break()
      endif()
    endforeach()
    if(class_source STREQUAL "")
      message(FATAL_ERROR
        "ControlFactory.Create() constructs ${class}, but its source class was not found")
    endif()
    string(REGEX MATCH
      "override[ \t]+string[ \t]+Kind[ \t]*\\([ \t]*\\)[ \t]*\\{[^\n]*return[ \t]+\"([^\"]+)\""
      kind_match "${class_source}")
    if(NOT kind_match)
      # Every factory class must own its persistence tag. Inheriting
      # Control.Kind() would serialize the class as "Control" and lose type
      # information on reload; legacy "Control" documents are not inferred.
      message(FATAL_ERROR
        "ControlFactory.Create() constructs ${class}, but its own Kind() override was not found")
    endif()
    set(label "${CMAKE_MATCH_1}")
    list(FIND kind_labels "${label}" duplicate)
    if(NOT duplicate EQUAL -1)
      message(FATAL_ERROR
        "ControlFactory Kind() collision: ${class} returns duplicate label ${label}")
    endif()
    list(APPEND kind_labels "${label}")
  endforeach()
endif()
list(LENGTH kinds kind_count)
message(STATUS
  "control factory policy OK: ${kind_count} built-in kind entries and constructor branches have unique Kind() labels")
