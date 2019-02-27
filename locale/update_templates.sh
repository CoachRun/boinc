#!/bin/bash

set -e # abort if a command exits non-zero

# update template files from source and send them to transifex
# Then commit and push changes.

testmode=0
if test $# -gt 0; then
  if test $1 = "-t"; then
    testmode=1
  else
    echo "Usage: $0 [-t]"
    echo "     -t  testmode (don't commit or push to git repository)"
    exit 1
  fi
fi

# find source root upward from CWD
while ! test -r .tx/config; do
  cd ..
  test "`pwd`" = "/" && echo "no source directory found" >&2 && exit
done

command -v pocompile >/dev/null 2>&1 || { echo >&2 "xgettext (gettext) is needed but not installed.  Aborting."; exit 1; }
command -v tx >/dev/null 2>&1 || { echo >&2 "tx (transifex-client) is needed but not installed.  Aborting."; exit 1; }

# check if working directory is clean to ensure we only commit localization changes
if test 0 -ne `git status -s -uno |wc -l`; then
  echo "Please commit your pending changes first"
  exit 1
fi

srcdir=`pwd`
YEAR=`date -u +"%Y"`
DATE=`date -u +"%Y-%m-%d %H:%M %Z"`
VERSION=`git rev-parse HEAD`
HEADER_FILE="${srcdir}/locale/templates/header.txt"
GEN_HEADER_ADD="${srcdir}/locale/templates/header-generic-web.txt"

cd ${srcdir}
echo "building localization template for Manager"
TMPL_NAME="manager"
TMPL_FILE="${srcdir}/locale/templates/BOINC-Manager.pot"
FILE_LIST="clientgui/*.cpp clientgui/msw/*.cpp clientgui/mac/*.cpp clientgui/gtk/*.cpp"

sed -e "s/@YEAR@/$YEAR/" -e "s/@DATE@/$DATE/" -e "s/@VERSION@/$VERSION/" -e "s/@TMPL_NAME@/$TMPL_NAME/" ${HEADER_FILE} > ${TMPL_FILE}
xgettext --from-code=UTF-8 --omit-header --add-comments -o - --keyword=_ -C ${FILE_LIST} >> ${TMPL_FILE}

cd ${srcdir}
echo "building localization template for Client"
TMPL_NAME="client"
TMPL_FILE="${srcdir}/locale/templates/BOINC-Client.pot"
FILE_LIST="client/*.cpp sched/*.cpp"

sed -e "s/@YEAR@/$YEAR/" -e "s/@DATE@/$DATE/" -e "s/@VERSION@/$VERSION/" -e "s/@TMPL_NAME@/$TMPL_NAME/" ${HEADER_FILE} > ${TMPL_FILE}
xgettext --omit-header --add-comments -o - --keyword=_ -C ${FILE_LIST} >> ${TMPL_FILE}

cd ${srcdir}
echo "building localization template for Setup tool"
TMPL_NAME="setup"
TMPL_FILE="${srcdir}/locale/templates/BOINC-Setup.pot"
FILE_LIST="mac_installer/*.cpp"

sed -e "s/@YEAR@/$YEAR/" -e "s/@DATE@/$DATE/" -e "s/@VERSION@/$VERSION/" -e "s/@TMPL_NAME@/$TMPL_NAME/" ${HEADER_FILE} > ${TMPL_FILE}
xgettext --omit-header --add-comments -o - --keyword=_ -C ${FILE_LIST} >> ${TMPL_FILE}

cd ${srcdir}
echo "building localization template for generic website"
TMPL_NAME="project generic website"
TMPL_FILE="${srcdir}/locale/templates/BOINC-Project-Generic.pot"
FILE_LIST="html/inc/*.inc html/user/*.php html/project.sample/*.inc"

sed -e "s/@YEAR@/$YEAR/" -e "s/@DATE@/$DATE/" -e "s/@VERSION@/$VERSION/" -e "s/@TMPL_NAME@/$TMPL_NAME/" ${HEADER_FILE} > ${TMPL_FILE}
cat ${GEN_HEADER_ADD} >> ${TMPL_FILE}
xgettext --omit-header --add-comments -o - --keyword=tra -L PHP ${FILE_LIST} >> ${TMPL_FILE}

#cd ${srcdir}
#echo "building localization template for BOINC website"
#TMPL_NAME="website"
#TMPL_FILE="${srcdir}/locale/templates/BOINC-Web.pot"
#FILE_LIST="doc/account_managers.inc doc/addons.php doc/docutil.php doc/download.php doc/index.php doc/help.php doc/help_funcs.php doc/links.php doc/logo.php doc/projects.php doc/download_util.inc doc/projects.inc html/inc/news.inc"

#sed -e "s/@YEAR@/$YEAR/" -e "s/@DATE@/$DATE/" -e "s/@VERSION@/$VERSION/" -e "s/@TMPL_NAME@/$TMPL_NAME/" ${HEADER_FILE} > ${TMPL_FILE}
#cat ${GEN_HEADER_ADD} >> ${TMPL_FILE}
#xgettext --omit-header --add-comments -o - --keyword=tra -L PHP ${FILE_LIST} >> ${TMPL_FILE}

# The Android template is updated using Android Studio
# The BOINC-Drupal.pot template is updated by Einstein@Home

git add -u # only update already tracked files (will not track new files)
if test $testmode -eq 0; then
  git commit -m "Locale: Update localization template files [skip ci]"
  git push
  tx push -s
else
  echo "working directory prepared for commit, inspect changes with 'git diff --cached'"
fi

exit 0
