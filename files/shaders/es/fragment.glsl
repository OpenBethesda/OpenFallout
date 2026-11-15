#version 300 es
// PROTOTYPE (ANGLE spike): goes in front of every fragment shader after it has been through es/rules.txt
precision highp float;
precision highp int;
precision highp sampler2D;
precision highp sampler2DArray;
precision highp sampler2DShadow;
#define OF_GLES 1
#define varying in
#define texture2D texture
#define texture2DLod textureLod
#define shadow2DProj(s, c) vec4(textureProj(s, c))
uniform mat4 osg_ModelViewMatrix;
uniform mat4 osg_ProjectionMatrix;
uniform mat3 osg_NormalMatrix;
layout(location = 0) out vec4 of_FragData[2];
