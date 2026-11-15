#version 300 es
// PROTOTYPE (ANGLE spike): goes in front of every vertex shader after it has been through es/rules.txt
precision highp float;
precision highp int;
precision highp sampler2D;
precision highp sampler2DArray;
precision highp sampler2DShadow;
#define OF_GLES 1
#define attribute in
#define varying out
#define texture2D texture
#define texture2DLod textureLod
in vec4 osg_Vertex;
in vec3 osg_Normal;
in vec4 osg_Color;
in vec4 osg_MultiTexCoord0;
in vec4 osg_MultiTexCoord1;
in vec4 osg_MultiTexCoord2;
in vec4 osg_MultiTexCoord3;
in vec4 osg_MultiTexCoord4;
in vec4 osg_MultiTexCoord5;
in vec4 osg_MultiTexCoord6;
in vec4 osg_MultiTexCoord7;
uniform mat4 osg_ModelViewMatrix;
uniform mat4 osg_ProjectionMatrix;
uniform mat4 osg_ModelViewProjectionMatrix;
uniform mat3 osg_NormalMatrix;
vec4 of_ClipVertex;
