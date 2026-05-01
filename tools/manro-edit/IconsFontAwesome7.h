#pragma once

// Font Awesome 7 Free icon codepoints for ImGui.
// Verified against Font Awesome 7 Free-Solid-900.otf.
//
// Usage with ImGui font loading:
//   static const ImWchar ranges[] = { ICON_MIN_FA7, ICON_MAX_16_FA7, 0 };

#define ICON_MIN_FA7        0xe005
#define ICON_MAX_16_FA7     0xf8ff
#define ICON_MAX_FA7        0x1fac1

// Navigation & Arrows
#define ICON_FA7_ARROW_LEFT                              "\xef\x81\xa0"  // U+F060
#define ICON_FA7_ARROW_RIGHT                             "\xef\x81\xa1"  // U+F061
#define ICON_FA7_ARROW_UP                                "\xef\x81\xa2"  // U+F062
#define ICON_FA7_ARROW_DOWN                              "\xef\x81\xa3"  // U+F063
#define ICON_FA7_ARROWS_UP_DOWN_LEFT_RIGHT               "\xef\x81\x87"  // U+F047
#define ICON_FA7_ANGLES_LEFT                             "\xef\x84\x80"  // U+F100
#define ICON_FA7_ANGLES_RIGHT                            "\xef\x84\x81"  // U+F101
#define ICON_FA7_ANGLE_LEFT                              "\xef\x84\x84"  // U+F104
#define ICON_FA7_ANGLE_RIGHT                             "\xef\x84\x85"  // U+F105
#define ICON_FA7_ANGLE_UP                                "\xef\x84\x86"  // U+F106
#define ICON_FA7_ANGLE_DOWN                              "\xef\x84\x87"  // U+F107
#define ICON_FA7_CHEVRON_LEFT                            "\xef\x81\x93"  // U+F053
#define ICON_FA7_CHEVRON_RIGHT                           "\xef\x81\x94"  // U+F054
#define ICON_FA7_CHEVRON_UP                              "\xef\x81\xb7"  // U+F077
#define ICON_FA7_CHEVRON_DOWN                            "\xef\x81\xb8"  // U+F078

// Transform / Gizmo
#define ICON_FA7_ROTATE                                  "\xef\x8b\xb1"  // U+F2F1
#define ICON_FA7_ROTATE_RIGHT                            "\xef\x80\x9e"  // U+F01E
#define ICON_FA7_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER      "\xef\x90\xa4"  // U+F424
#define ICON_FA7_DOWN_LEFT_AND_UP_RIGHT_TO_CENTER        "\xef\x90\xa2"  // U+F422
#define ICON_FA7_EXPAND                                  "\xef\x81\xa5"  // U+F065
#define ICON_FA7_COMPRESS                                "\xef\x81\xa6"  // U+F066

// Objects & Shapes
#define ICON_FA7_CUBE                                    "\xef\x86\xb2"  // U+F1B2
#define ICON_FA7_CUBES                                   "\xef\x86\xb3"  // U+F1B3
#define ICON_FA7_GLOBE                                   "\xef\x82\xac"  // U+F0AC
#define ICON_FA7_CIRCLE                                  "\xef\x84\x91"  // U+F111
#define ICON_FA7_SQUARE                                  "\xef\x83\x88"  // U+F0C8
#define ICON_FA7_SHAPES                                  "\xef\x98\x9f"  // U+F61F
#define ICON_FA7_DRAW_POLYGON                            "\xef\x97\xae"  // U+F5EE

// Files & Folders
#define ICON_FA7_FILE                                    "\xef\x85\x9b"  // U+F15B
#define ICON_FA7_FILE_LINES                              "\xef\x85\x9c"  // U+F15C
#define ICON_FA7_FILE_IMPORT                             "\xef\x95\xaf"  // U+F56F
#define ICON_FA7_FILE_EXPORT                             "\xef\x95\xae"  // U+F56E
#define ICON_FA7_FOLDER                                  "\xef\x81\xbb"  // U+F07B
#define ICON_FA7_FOLDER_OPEN                             "\xef\x81\xbc"  // U+F07C
#define ICON_FA7_FOLDER_PLUS                             "\xef\x99\x9e"  // U+F65E
#define ICON_FA7_FLOPPY_DISK                             "\xef\x83\x87"  // U+F0C7

// Actions
#define ICON_FA7_XMARK                                   "\xef\x80\x8d"  // U+F00D
#define ICON_FA7_CHECK                                   "\xef\x80\x8c"  // U+F00C
#define ICON_FA7_MINUS                                   "\xef\x81\xa8"  // U+F068
#define ICON_FA7_TRASH                                   "\xef\x87\xb8"  // U+F1F8
#define ICON_FA7_TRASH_CAN                               "\xef\x8b\xad"  // U+F2ED
#define ICON_FA7_PEN                                     "\xef\x8c\x84"  // U+F304
#define ICON_FA7_PEN_TO_SQUARE                           "\xef\x81\x84"  // U+F044
#define ICON_FA7_COPY                                    "\xef\x83\x85"  // U+F0C5
#define ICON_FA7_PASTE                                   "\xef\x83\xaa"  // U+F0EA
#define ICON_FA7_SCISSORS                                "\xef\x83\x84"  // U+F0C4
#define ICON_FA7_CLONE                                   "\xef\x89\x8d"  // U+F24D
#define ICON_FA7_GRIP                                    "\xef\x96\x8d"  // U+F58D
#define ICON_FA7_GRIP_VERTICAL                           "\xef\x96\x8e"  // U+F58E

// Search & Filter
#define ICON_FA7_MAGNIFYING_GLASS                        "\xef\x80\x82"  // U+F002
#define ICON_FA7_FILTER                                  "\xef\x82\xb0"  // U+F0B0
#define ICON_FA7_SORT                                    "\xef\x83\x9c"  // U+F0DC

// Media & Controls
#define ICON_FA7_PLAY                                    "\xef\x81\x8b"  // U+F04B
#define ICON_FA7_PAUSE                                   "\xef\x81\x8c"  // U+F04C
#define ICON_FA7_STOP                                    "\xef\x81\x8d"  // U+F04D

// UI Elements
#define ICON_FA7_BARS                                    "\xef\x83\x89"  // U+F0C9
#define ICON_FA7_ELLIPSIS                                "\xef\x85\x81"  // U+F141
#define ICON_FA7_ELLIPSIS_VERTICAL                       "\xef\x85\x82"  // U+F142
#define ICON_FA7_GEAR                                    "\xef\x80\x93"  // U+F013
#define ICON_FA7_GEARS                                   "\xef\x82\x85"  // U+F085
#define ICON_FA7_SLIDERS                                 "\xef\x87\x9e"  // U+F1DE
#define ICON_FA7_WRENCH                                  "\xef\x82\xad"  // U+F0AD
#define ICON_FA7_SCREWDRIVER_WRENCH                      "\xef\x9f\x99"  // U+F7D9
#define ICON_FA7_HAMMER                                  "\xef\x9b\xa3"  // U+F6E3
#define ICON_FA7_TOOLBOX                                 "\xef\x95\x92"  // U+F552
#define ICON_FA7_PALETTE                                 "\xef\x94\xbf"  // U+F53F

// Layout & Grid
#define ICON_FA7_TABLE_CELLS                             "\xef\x80\x8a"  // U+F00A
#define ICON_FA7_TABLE_COLUMNS                           "\xef\x83\x9b"  // U+F0DB
#define ICON_FA7_TABLE_LIST                              "\xef\x80\x8b"  // U+F00B
#define ICON_FA7_BORDER_ALL                              "\xef\xa1\x8c"  // U+F84C
#define ICON_FA7_MAGNET                                  "\xef\x81\xb6"  // U+F076
#define ICON_FA7_GRIP_LINES                              "\xef\x9e\xa4"  // U+F7A4
#define ICON_FA7_LAYER_GROUP                             "\xef\x97\xbd"  // U+F5FD
#define ICON_FA7_OBJECT_GROUP                            "\xef\x89\x87"  // U+F247
#define ICON_FA7_OBJECT_UNGROUP                          "\xef\x89\x88"  // U+F248

// Lighting & Environment
#define ICON_FA7_SUN                                     "\xef\x86\x85"  // U+F185
#define ICON_FA7_MOON                                    "\xef\x86\x86"  // U+F186
#define ICON_FA7_LIGHTBULB                               "\xef\x83\xab"  // U+F0EB
#define ICON_FA7_BOLT                                    "\xef\x83\xa7"  // U+F0E7
#define ICON_FA7_CLOUD                                   "\xef\x83\x82"  // U+F0C2
#define ICON_FA7_CLOUD_SUN                               "\xef\x9b\x84"  // U+F6C4
#define ICON_FA7_MOUNTAIN_SUN                            "\xee\x94\xae"  // U+E52E
#define ICON_FA7_TREE                                    "\xef\x86\xbb"  // U+F1BB
#define ICON_FA7_WATER                                   "\xef\x9d\xb3"  // U+F773
#define ICON_FA7_WIND                                    "\xef\x9c\xae"  // U+F72E

// Camera & View
#define ICON_FA7_CAMERA                                  "\xef\x80\xb0"  // U+F030
#define ICON_FA7_VIDEO                                   "\xef\x80\xbd"  // U+F03D
#define ICON_FA7_EYE                                     "\xef\x81\xae"  // U+F06E
#define ICON_FA7_EYE_SLASH                               "\xef\x81\xb0"  // U+F070
#define ICON_FA7_CROSSHAIRS                              "\xef\x81\x9b"  // U+F05B

// Status & Info
#define ICON_FA7_CIRCLE_INFO                             "\xef\x81\x9a"  // U+F05A
#define ICON_FA7_CIRCLE_CHECK                            "\xef\x81\x98"  // U+F058
#define ICON_FA7_CIRCLE_EXCLAMATION                      "\xef\x81\xaa"  // U+F06A
#define ICON_FA7_CIRCLE_XMARK                            "\xef\x81\x97"  // U+F057
#define ICON_FA7_TRIANGLE_EXCLAMATION                    "\xef\x81\xb1"  // U+F071
#define ICON_FA7_BUG                                     "\xef\x86\x88"  // U+F188
#define ICON_FA7_CIRCLE_QUESTION                         "\xef\x81\x99"  // U+F059

// Box / Package
#define ICON_FA7_BOX                                     "\xef\x91\xa6"  // U+F466
#define ICON_FA7_BOX_OPEN                                "\xef\x92\x9e"  // U+F49E
#define ICON_FA7_BOX_ARCHIVE                             "\xef\x86\x87"  // U+F187
#define ICON_FA7_BOXES_STACKED                           "\xef\x91\xa8"  // U+F468

// Misc
#define ICON_FA7_HOUSE                                   "\xef\x80\x95"  // U+F015
#define ICON_FA7_IMAGE                                   "\xef\x80\xbe"  // U+F03E
#define ICON_FA7_MAP                                     "\xef\x89\xb9"  // U+F279
#define ICON_FA7_MAP_PIN                                 "\xef\x89\xb6"  // U+F276
#define ICON_FA7_LOCATION_DOT                            "\xef\x8f\x85"  // U+F3C5
#define ICON_FA7_TAG                                     "\xef\x80\xab"  // U+F02B
#define ICON_FA7_TAGS                                    "\xef\x80\xac"  // U+F02C
#define ICON_FA7_LINK                                    "\xef\x83\x81"  // U+F0C1
#define ICON_FA7_CODE                                    "\xef\x84\xa1"  // U+F121
#define ICON_FA7_TERMINAL                                "\xef\x84\xa0"  // U+F120
#define ICON_FA7_DOWNLOAD                                "\xef\x80\x99"  // U+F019
#define ICON_FA7_UPLOAD                                  "\xef\x82\x93"  // U+F093
#define ICON_FA7_CLOCK                                   "\xef\x80\x97"  // U+F017
#define ICON_FA7_SPINNER                                 "\xef\x84\x90"  // U+F110
#define ICON_FA7_LOCK                                    "\xef\x80\xa3"  // U+F023
#define ICON_FA7_UNLOCK                                  "\xef\x82\x9c"  // U+F09C
#define ICON_FA7_USER                                    "\xef\x80\x87"  // U+F007
#define ICON_FA7_USERS                                   "\xef\x83\x80"  // U+F0C0
#define ICON_FA7_POWER_OFF                               "\xef\x80\x91"  // U+F011
#define ICON_FA7_HAND_POINTER                            "\xef\x89\x9a"  // U+F25A

// Dashboard
#define ICON_FA7_GAUGE_HIGH                              "\xef\x98\xa5"  // U+F625

// Undo / Redo
#define ICON_FA7_ARROW_ROTATE_LEFT                       "\xef\x83\xa2"  // U+F0E2
#define ICON_FA7_ARROW_ROTATE_RIGHT                      "\xef\x80\x9e"  // U+F01E
#define ICON_FA7_CLOCK_ROTATE_LEFT                       "\xef\x87\x9a"  // U+F1DA
