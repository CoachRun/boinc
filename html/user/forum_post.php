<?php
// This file is part of BOINC.
// http://boinc.berkeley.edu
// Copyright (C) 2008 University of California
//
// BOINC is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// BOINC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with BOINC.  If not, see <http://www.gnu.org/licenses/>.

// This file allows you to create a new thread in a forum
// At first it displays an input box and when you submit
// it will apply the changes by calling methods on the forum

require_once('../inc/forum_email.inc');
require_once('../inc/forum.inc');
require_once('../inc/bbcode_html.inc');
require_once('../inc/akismet.inc');
require_once('../inc/news.inc');

check_get_args(array("id", "title", "force_title", "tnow", "ttok", "export"));

$logged_in_user = get_logged_in_user();
BoincForumPrefs::lookup($logged_in_user);

check_banished($logged_in_user);

$forumid = get_int("id");
$forum = BoincForum::lookup_id($forumid);

if (DISABLE_FORUMS && !is_admin($logged_in_user)) {
    error_page("Forums are disabled");
}

if (!user_can_create_thread($logged_in_user, $forum)) {
    error_page(tra("Only project admins may create a thread here. However, you may reply to existing threads."));
}
check_post_access($logged_in_user, $forum);

$title = post_str("title", true);
if (!$title) $title = get_str("title", true);
$force_title = get_str("force_title", true);
$export = post_str("export", true);
$content = post_str("content", true);
$preview = post_str("preview", true);
$warning = null;

if ($content && $title && (!$preview)){
    if (post_str('add_signature', true) == "add_it"){
        $add_signature = true;    // set a flag and concatenate later
    }  else {
        $add_signature = false;
    }
    check_tokens($logged_in_user->authenticator);
    if (!akismet_check($logged_in_user, $content)) {
        $warning = tra("Your message was flagged as spam by the Akismet anti-spam system. Please modify your text and try again.");
        $preview = tra("Preview");
    } else {
        $thread = create_thread(
            $title, $content, $logged_in_user, $forum, $add_signature, $export
        );
        if ($thread) {
            header('Location: forum_thread.php?id=' . $thread->id);
        } else {
            error_page("Can't create thread.  $forum_error");
        }
    }
}

page_head(tra("Create new thread"),'','','', $bbcode_js);
show_forum_header($logged_in_user);

if ($warning) {
    echo "<p class=\"text-danger\">$warning</p>";
}

switch ($forum->parent_type) {
case 0:
    $category = BoincCategory::lookup_id($forum->category);
    show_forum_title($category, $forum, null);
    break;
case 1:
    show_team_forum_title($forum);
    break;
}

echo "<p></p>";

if ($preview == tra("Preview")) {
    panel(tra('Preview'),
        function() use($content, $title) {
            echo "<span class=lead>$title</span><p>\n";
            echo output_transform($content, null);
        }
    );
}

echo "<form action=\"forum_post.php?id=".$forum->id."\" method=\"POST\" name=\"post\" onsubmit=\"return checkForm(this)\">\n";
echo form_tokens($logged_in_user->authenticator);

start_table();

row1(tra("Create a new thread"));
$submit_help = "";
$body_help = "";

if ($content && !$title) {
    $submit_help = "<br /><font color=\"red\">".tra("Remember to add a title")."</font>";
}

if ($force_title && $title){
    row2(tra("Title"), htmlspecialchars($title)."<input type=\"hidden\" name=\"title\" value=\"".htmlspecialchars($title)."\">");
} else {
    row2(tra("Title").$submit_help,
        '<input type="text" class="form-control" name="title" value="'.htmlspecialchars($title).'">'
    );
}

row2_init(tra("Message").bbcode_info().post_warning($forum).$body_help, "");
start_table();
echo $bbcode_html;
end_table();
echo '<textarea class="form-control" name="content" rows="12" cols="80">'.htmlspecialchars($content).'</textarea>';
echo "</td></tr>";

if (!$logged_in_user->prefs->no_signature_by_default) {
    $enable_signature="checked=\"true\"";
} else {
    $enable_signature="";
}

if (is_news_forum($forum)) {
    row2("", "<input name=export type=checkbox> ".tra("Show this item as a Notice in the BOINC Manager")."<br><p class=\"text-muted\">".tra("Do so only for items likely to be of interest to all volunteers.")."</p>");
}
row2("", "<input name=\"add_signature\" value=\"add_it\" ".$enable_signature." type=\"checkbox\"> ".tra("Add my signature to this post"));
row2("", "<input class=\"btn btn-primary\" type=\"submit\" name=\"preview\" value=\"".tra("Preview")."\"> <input class=\"btn btn-success\" type=\"submit\" value=\"".tra("OK")."\">");


end_table();

echo "</form>\n";

page_tail();

$cvs_version_tracker[]="\$Id$";
?>
