# SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(jobsdatabasetest
    jobsdatabasetest.cpp
    ../src/jobstage.cpp
    ../src/jobsdatabase.cpp
)

target_include_directories(jobsdatabasetest PRIVATE ../src)

target_link_libraries(jobsdatabasetest PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Sql
    Qt6::Test
    KF6::I18n
)

add_test(NAME jobsdatabasetest COMMAND jobsdatabasetest)

add_executable(sankeylayouttest
    sankeylayouttest.cpp
    ../src/jobstage.cpp
    ../src/jobsdatabase.cpp
    ../src/sankeymodel.cpp
)

target_include_directories(sankeylayouttest PRIVATE ../src)

target_link_libraries(sankeylayouttest PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Sql
    Qt6::Test
    KF6::I18n
)

add_test(NAME sankeylayouttest COMMAND sankeylayouttest)

add_executable(jobeditmodeltest
    jobeditmodeltest.cpp
    ../src/jobstage.cpp
    ../src/jobsdatabase.cpp
    ../src/jobsmodel.cpp
    ../src/jobeditmodel.cpp
    ../src/jobfieldcatalog.cpp
)

target_include_directories(jobeditmodeltest PRIVATE ../src)

target_link_libraries(jobeditmodeltest PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Sql
    Qt6::Test
    KF6::I18n
)

add_test(NAME jobeditmodeltest COMMAND jobeditmodeltest)
