# The OpenSceneGraph that the Metal build draws with: OpenMW's fork, branch 3.6, at one commit. Read by
# build_osg_gles3.sh (`source`) and by osg_gles3.py. The same commit is the one the desktop OpenGL build's dependency
# set (openmw-deps, port openmw-osg) is built from. To change it: docs/openfallout/osg-gles3.md.
OSG_REPOSITORY='https://github.com/OpenMW/osg'
OSG_BRANCH='3.6'
OSG_COMMIT='e568aace1e62de021df9955c62429cbe55516fd3'
# SHA-256 of https://github.com/OpenMW/osg/archive/<OSG_COMMIT>.tar.gz
OSG_ARCHIVE_SHA256='03f14ebb0a4ff34c41128aa2a2ecef90e0276b19c94811fb24eb110a59139ff3'
