<?php
// This file is part of BOINC.
// http://boinc.berkeley.edu
// Copyright (C) 2011 University of California
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

// Handler for remote job submission.
// See https://boinc.berkeley.edu/trac/wiki/RemoteJobs

require_once("../inc/boinc_db.inc");
require_once("../inc/submit_db.inc");
require_once("../inc/xml.inc");
require_once("../inc/dir_hier.inc");
require_once("../inc/result.inc");
require_once("../inc/submit_util.inc");

error_reporting(E_ALL);
ini_set('display_errors', true);
ini_set('display_startup_errors', true);

function get_wu($name) {
    $name = BoincDb::escape_string($name);
    $wu = BoincWorkunit::lookup("name='$name'");
    if (!$wu) xml_error(-1, "no job named $name was found");
    return $wu;
}

function get_submit_app($name) {
    $name = BoincDb::escape_string($name);
    $app = BoincApp::lookup("name='$name'");
    if (!$app) xml_error(-1, "no app named $name was found");
    return $app;
}

// estimate FLOP count for a batch.
// If estimates aren't included in the job descriptions,
// use what's in the input template
//
function batch_flop_count($r, $template) {
    $x = 0;
    $t = 0;
    if ($template) {
        $t = (double)$template->workunit->rsc_fpops_est;
    }
    foreach($r->batch->job as $job) {
        $y = (double)$job->rsc_fpops_est;
        if ($y) {
            $x += $y;
        } else if ($t) {
            $x += $t;
        } else {
            xml_error(-1, "no rsc_fpops_est given for job");
        }
    }
    return $x;
}

// estimate project FLOPS based on recent average credit
//
function project_flops() {
    $x = BoincUser::sum("expavg_credit");
    if ($x == 0) $x = 200;
    $y = 1e9*$x/200;
    return $y;
}

function est_elapsed_time($r, $template) {
    // crude estimate: batch FLOPs / project FLOPS
    //
    return batch_flop_count($r, $template) / project_flops();
}

// if batch-level input template filename was given, read it;
// else if standard file (app_in) is present, read it;
// else return null
// Note: input templates may also be given per job
//
function read_input_template($app, $r) {
    if ((isset($r->batch)) && (isset($r->batch->workunit_template_file)) && ($r->batch->workunit_template_file)) {
        $path = project_dir() . "/templates/".$r->batch->workunit_template_file;
    } else {
        $path = project_dir() . "/templates/$app->name"."_in";
    }
    if (file_exists($path)) {
        $x = simplexml_load_file($path);
        if (!$x) {
            xml_error(-1, "couldn't parse input template file $path");
        }
        return $x;
    } else {
        return null;
    }
}

function check_max_jobs_in_progress($r, $user_submit) {
    if (!$user_submit->max_jobs_in_progress) return;
    $query = "select count(*) as total from DBNAME.result, DBNAME.batch where batch.user_id=$userid and result.batch = batch.id and result.server_state<".RESULT_SERVER_STATE_OVER;
    $db = BoincDb::get();
    $n = $db->get_int($query, 'total');
    if ($n === false) return;
    if ($n + count($r->batch->job) > $user_submit->max_jobs_in_progress) {
        xml_error(-1, "limit on jobs in progress exceeded");
    }
}

function estimate_batch($r) {
    xml_start_tag("estimate_batch");
    $app = get_submit_app((string)($r->batch->app_name));
    list($user, $user_submit) = authenticate_user($r, $app);

    $template = read_input_template($app, $r);
    $e = est_elapsed_time($r, $template);
    echo "<seconds>$e</seconds>
        </estimate_batch>
    ";
}

// Verify that the number of input files for each job agrees with its template
// The arg is the batch-level template, if any.
// Jobs may have their own templates.
//
function validate_batch($jobs, $template) {
    $i = 0;
    $n = count($template->file_info);
    foreach($jobs as $job) {
        $m = count($job->input_files);
        if ($n != $m) {
            xml_error(-1, "wrong # of input files for job $i: need $n, got $m");
        }
        $i++;
    }
}

$fanout = parse_config(get_config(), "<uldl_dir_fanout>");

// stage a file, and return the physical name
//
function stage_file($file) {
    global $fanout;
    $download_dir = parse_config(get_config(), "<download_dir>");

    switch ($file->mode) {
    case "semilocal":
    case "local":
        // read the file (from disk or network) to get MD5.
        // Copy to download hier, using a physical name based on MD5
        //
        $md5 = md5_file($file->source);
        if (!$md5) {
            xml_error(-1, "Can't get MD5 of file $file->source");
        }
        $name = job_file_name($md5);
        $path = dir_hier_path($name, $download_dir, $fanout);
        if (file_exists($path)) return $name;
        if (!copy($file->source, $path)) {
            xml_error(-1, "can't copy file from $file->source to $path");
        }
        return $name;
    case "local_staged":
        return $file->source;
    case "inline":
        $md5 = md5($file->source);
        if (!$md5) {
            xml_error(-1, "Can't get MD5 of inline data");
        }
        $name = job_file_name($md5);
        $path = dir_hier_path($name, $download_dir, $fanout);
        if (file_exists($path)) return $name;
        if (!file_put_contents($path, $file->source)) {
            xml_error(-1, "can't write to file $path");
        }
        return $name;
    }
    xml_error(-1, "unsupported file mode: $file->mode");
}

// stage all the files
//
function stage_files(&$jobs) {
    foreach($jobs as $job) {
        foreach ($job->input_files as $file) {
            if ($file->mode != "remote") {
                $file->name = stage_file($file);
            }
        }
    }
}

// submit a list of jobs with a single create_work command.
//
function submit_jobs(
    $jobs, $job_params, $app, $batch_id, $priority, $app_version_num,
    $input_template_filename,        // batch-level; can also specify per job
    $output_template_filename
) {
    global $input_templates, $output_templates;

    // make a string to pass to create_work;
    // one line per job
    //
    $x = "";
    foreach($jobs as $job) {
        if ($job->name) {
            $x .= " --wu_name $job->name";
        }
        if ($job->command_line) {
            $x .= " --command_line \"$job->command_line\"";
        }
        if ($job->target_team) {
            $x .= " --target_team $job->target_team";
        } elseif ($job->target_user) {
            $x .= " --target_user $job->target_user";
        } elseif ($job->target_host) {
            $x .= " --target_host $job->target_host";
        }
        foreach ($job->input_files as $file) {
            if ($file->mode == "remote") {
                $x .= " --remote_file $file->url $file->nbytes $file->md5";
            } else {
                $x .= " $file->name";
            }
        }
        if ($job->input_template) {
            $f = $input_templates[$job->input_template_xml];
            $x .= " --wu_template $f";
        }
        if ($job->output_template) {
            $f = $output_templates[$job->output_template_xml];
            $x .= " --result_template $f";
        }
        $x .= "\n";
    }

    $errfile = "/tmp/create_work_" . getmypid() . ".err";
    $cmd = "cd " . project_dir() . "; ./bin/create_work --appname $app->name --batch $batch_id --priority $priority";
    if ($input_template_filename) {
        $cmd .= " --wu_template templates/$input_template_filename";
    }
    if ($output_template_filename) {
        $cmd .= " --result_template templates/$output_template_filename";
    }
    if ($app_version_num) {
        $cmd .= " --app_version_num $app_version_num";
    }
    if ($job_params->rsc_disk_bound) {
        $cmd .= " --rsc_disk_bound $job_params->rsc_disk_bound";
    }
    if ($job_params->rsc_fpops_est) {
        $cmd .= " --rsc_fpops_est $job_params->rsc_fpops_est";
    }
    if ($job_params->rsc_fpops_bound) {
        $cmd .= " --rsc_fpops_bound $job_params->rsc_fpops_bound";
    }
    if ($job_params->rsc_memory_bound) {
        $cmd .= " --rsc_memory_bound $job_params->rsc_memory_bound";
    }
    if ($job_params->delay_bound) {
        $cmd .= " --delay_bound $job_params->delay_bound";
    }
    $cmd .= " --stdin >$errfile 2>&1";
    $h = popen($cmd, "w");
    if ($h === false) {
        xml_error(-1, "can't run create_work");
    }
    fwrite($h, $x);
    $ret = pclose($h);
    if ($ret) {
        $err = file_get_contents($errfile);
        unlink($errfile);
        xml_error(-1, "create_work failed: $err");
    }
    unlink($errfile);
}

// lists of arrays for job-level templates;
// each maps template to filename
//
$input_templates = array();
$output_templates = array();

// The job specifies an input template.
// Check whether the template is already in our map.
// If not, write it to a temp file.
//
function make_input_template($job) {
    global $input_templates;
    if (!array_key_exists($job->input_template_xml, $input_templates)) {
        $f = tempnam("/tmp", "input_template_");
        //echo "writing wt $f\n";
        file_put_contents($f, $job->input_template_xml);
        $input_templates[$job->input_template_xml] = $f;
    //} else {
    //    echo "dup wu template\n";
    }
}

// same for output templates.
// A little different because these have to exist for life of job.
// Store them in templates/tmp/, with content-based filenames
//
function make_output_template($job) {
    global $output_templates;
    if (!array_key_exists($job->output_template_xml, $output_templates)) {
        $m = md5($job->output_template_xml);
        $filename = "templates/tmp/$m";
        $path = "../../$filename";
        if (!file_exists($filename)) {
            @mkdir("../../templates/tmp");
            file_put_contents($path, $job->output_template_xml);
        }
        $output_templates[$job->output_template_xml] = $filename;
    //} else {
    //    echo "dup result template\n";
    }
}

// delete per-job WU templates after creating jobs.
// (we can't delete result templates)
//
function delete_input_templates() {
    global $input_templates;
    foreach ($input_templates as $t => $f) {
        unlink($f);
    }
}

// convert job list from XML nodes to our own objects
//
function xml_get_jobs($r) {
    $jobs = array();
    foreach($r->batch->job as $j) {
        $job = new StdClass;
        $job->input_files = array();
        $job->command_line = (string)$j->command_line;
        $job->target_team = (int)$j->target_team;
        $job->target_user = (int)$j->target_user;
        $job->target_host = (int)$j->target_host;
        $job->name = (string)$j->name;
        $job->rsc_fpops_est = (double)$j->rsc_fpops_est;
        $job->input_template = null;
        if ($j->input_template) {
            $job->input_template = $j->input_template;
            $job->input_template_xml = $j->input_template->asXML();
        }
        $job->output_template = null;
        if ($j->output_template) {
            $job->output_template = $j->output_template;
            $job->output_template_xml = $j->output_template->asXML();
        }
        foreach ($j->input_file as $f) {
            $file = new StdClass;
            $file->mode = (string)$f->mode;
            if ($file->mode == "remote") {
                $file->url = (string)$f->url;
                $file->nbytes = (double)$f->nbytes;
                $file->md5 = (string)$f->md5;
            } else {
                $file->source = (string)$f->source;
            }
            $job->input_files[] = $file;
        }
        $jobs[] = $job;
        if ($job->input_template) {
            make_input_template($job);
        }
        if ($job->output_template) {
            make_output_template($job);
        }
    }
    return $jobs;
}

// $r is a simplexml object encoding the request message
//
function submit_batch($r) {
    xml_start_tag("submit_batch");
    $app = get_submit_app((string)($r->batch->app_name));
    list($user, $user_submit) = authenticate_user($r, $app);
    $jobs = xml_get_jobs($r);
    $template = read_input_template($app, $r);
    if ($template) {
        validate_batch($jobs, $template);
    }
    stage_files($jobs);
    $njobs = count($jobs);
    $now = time();
    $app_version_num = (int)($r->batch->app_version_num);

    // batch may or may not already exist.
    // If it does, make sure it's owned by this user
    //
    $batch_id = (int)($r->batch->batch_id);
    if ($batch_id) {
        $batch = BoincBatch::lookup_id($batch_id);
        if (!$batch) {
            xml_error(-1, "no batch $batch_id");
        }
        if ($batch->user_id != $user->id) {
            xml_error(-1, "not owner of batch");
        }
        if ($batch->state != BATCH_STATE_INIT) {
            xml_error(-1, "batch not in init state");
        }
    }

    // - compute batch FLOP count
    // - run adjust_user_priorities to increment user_submit.logical_start_time
    // - use that for batch logical end time and job priority
    //
    $total_flops = 0;
    foreach($jobs as $job) {
        //print_r($job);
        if ($job->rsc_fpops_est) {
            $total_flops += $job->rsc_fpops_est;
        } else if ($job->input_template && $job->input_template->workunit->rsc_fpops_est) {
            $total_flops += (double) $job->input_template->workunit->rsc_fpops_est;
        } else if ($r->batch->job_params->rsc_fpops_est) {
            $total_flops += (double) $r->batch->job_params->rsc_fpops_est;
        } else {
            $x = (double) $template->workunit->rsc_fpops_est;
            if ($x) {
                $total_flops += $x;
            } else {
                xml_error(-1, "no rsc_fpops_est given");
            }
        }
    }
    $cmd = "cd " . project_dir() . "/bin; ./adjust_user_priority --user $user->id --flops $total_flops --app $app->name";
    $x = exec($cmd);
    if (!is_numeric($x) || (double)$x == 0) {
        xml_error(-1, "$cmd returned $x");
    }
    $let = (double)$x;

    if ($batch_id) {
        $njobs = count($jobs);
        $ret = $batch->update("njobs=$njobs, logical_end_time=$let");
        if (!$ret) xml_error(-1, "batch->update() failed");
    } else {
        $batch_name = (string)($r->batch->batch_name);
        $batch_name = BoincDb::escape_string($batch_name);
        $batch_id = BoincBatch::insert(
            "(user_id, create_time, njobs, name, app_id, logical_end_time, state) values ($user->id, $now, $njobs, '$batch_name', $app->id, $let, ".BATCH_STATE_INIT.")"
        );
        if (!$batch_id) {
            xml_error(-1, "Can't create batch: ".BoincDb::error());
        }
        $batch = BoincBatch::lookup_id($batch_id);
    }
    
    $job_params = new StdClass;
    $job_params->rsc_disk_bound = (double) $r->batch->job_params->rsc_disk_bound;
    $job_params->rsc_fpops_est = (double) $r->batch->job_params->rsc_fpops_est;
    $job_params->rsc_fpops_bound = (double) $r->batch->job_params->rsc_fpops_bound;
    $job_params->rsc_memory_bound = (double) $r->batch->job_params->rsc_memory_bound;
    $job_params->delay_bound = (double) $r->batch->job_params->delay_bound;
        // could add quorum-related stuff

    $input_template_filename = (string) $r->batch->input_template_filename;
    $output_template_filename = (string) $r->batch->output_template_filename;
        // possibly empty
    
    submit_jobs(
        $jobs, $job_params, $app, $batch_id, $let, $app_version_num,
        $input_template_filename,
        $output_template_filename
    );

    // set state to IN_PROGRESS only after creating jobs;
    // otherwise we might flag batch as COMPLETED
    //
    $ret = $batch->update("state= ".BATCH_STATE_IN_PROGRESS);
    if (!$ret) xml_error(-1, "batch->update() failed");

    echo "<batch_id>$batch_id</batch_id>
        </submit_batch>
    ";

    delete_input_templates();
}

function create_batch($r) {
    xml_start_tag("create_batch");
    $app = get_submit_app((string)($r->app_name));
    list($user, $user_submit) = authenticate_user($r, $app);
    $now = time();
    $batch_name = (string)($r->batch_name);
    $batch_name = BoincDb::escape_string($batch_name);
    $expire_time = (double)($r->expire_time);
    $batch_id = BoincBatch::insert(
        "(user_id, create_time, name, app_id, state, expire_time) values ($user->id, $now, '$batch_name', $app->id, ".BATCH_STATE_INIT.", $expire_time)"
    );
    if (!$batch_id) {
        xml_error(-1, "Can't create batch: ".BoincDb::error());
    }
    echo "<batch_id>$batch_id</batch_id>
        </create_batch>
    ";
}

function print_batch_params($batch, $get_cpu_time) {
    $app = BoincApp::lookup_id($batch->app_id);
    if (!$app) $app->name = "none";
    echo "
        <id>$batch->id</id>
        <create_time>$batch->create_time</create_time>
        <expire_time>$batch->expire_time</expire_time>
        <est_completion_time>$batch->est_completion_time</est_completion_time>
        <njobs>$batch->njobs</njobs>
        <fraction_done>$batch->fraction_done</fraction_done>
        <nerror_jobs>$batch->nerror_jobs</nerror_jobs>
        <state>$batch->state</state>
        <completion_time>$batch->completion_time</completion_time>
        <credit_estimate>$batch->credit_estimate</credit_estimate>
        <credit_canonical>$batch->credit_canonical</credit_canonical>
        <name>$batch->name</name>
        <app_name>$app->name</app_name>
";
    if ($get_cpu_time) {
        echo "        <total_cpu_time>".$batch->get_cpu_time()."</total_cpu_time>\n";
    }
}

function query_batches($r) {
    xml_start_tag("query_batches");
    list($user, $user_submit) = authenticate_user($r, null);
    $batches = BoincBatch::enum("user_id = $user->id");
    $get_cpu_time = (int)($r->get_cpu_time);
    foreach ($batches as $batch) {
        if ($batch->state == BATCH_STATE_RETIRED) continue;
        if ($batch->state < BATCH_STATE_COMPLETE) {
            $wus = BoincWorkunit::enum("batch = $batch->id");
            $batch = get_batch_params($batch, $wus);
        }
        echo "    <batch>\n";
        print_batch_params($batch, $get_cpu_time);
        echo "   </batch>\n";
    }
    echo "</query_batches>\n";
}

function n_outfiles($wu) {
    $path = project_dir() . "/$wu->output_template_filename";
    $r = simplexml_load_file($path);
    return count($r->file_info);
}

// show status of job.
// done:
// unsent:
// in_progress:
// error:

function show_job_details($wu) {
    if ($wu->error_mask & WU_ERROR_COULDNT_SEND_RESULT) {
        echo "   <error>couldnt_send_result</error>\n";
    }
    if ($wu->error_mask & WU_ERROR_TOO_MANY_ERROR_RESULTS) {
        echo "   <error>too_many_error_results</error>\n";
    }
    if ($wu->error_mask & WU_ERROR_TOO_MANY_SUCCESS_RESULTS) {
        echo "   <error>too_many_success_results</error>\n";
    }
    if ($wu->error_mask & WU_ERROR_TOO_MANY_TOTAL_RESULTS) {
        echo "   <error>too_many_total_results</error>\n";
    }
    if ($wu->error_mask & WU_ERROR_CANCELLED) {
        echo "   <error>cancelled</error>\n";
    }
    if ($wu->error_mask & WU_ERROR_NO_CANONICAL_RESULT) {
        echo "   <error>no_canonical_result</error>\n";
    }
    $results = BoincResult::enum("workunitid=$wu->id");
    $in_progress = 0;
    foreach ($results as $r) {
        switch ($r->server_state) {
        case RESULT_SERVER_STATE_IN_PROGRESS:
            $in_progress++;
            break;
        }
        if ($wu->error_mask && ($r->outcome == RESULT_OUTCOME_CLIENT_ERROR)) {
            echo "            <exit_status>$r->exit_status</exit_status>\n";
        }
        if ($r->id == $wu->canonical_resultid) {
            echo "            <cpu_time>$r->cpu_time</cpu_time>\n";
        }
    }
    if ($wu->error_mask) {
        echo "            <status>error</status>\n";
        return;
    }

    if ($wu->canonical_resultid) {
        echo "            <status>done</status>\n";
    } else {
        if ($in_progress > 0) {
            echo "            <status>in_progress</status>\n";
        } else {
            echo "            <status>queued</status>\n";
        }
    }
}

// return a batch specified by the command, using either ID or name
//
function get_batch($r) {
    $batch = NULL;
    if (!empty($r->batch_id)) {
        $batch_id = (int)($r->batch_id);
        $batch = BoincBatch::lookup_id($batch_id);
    } else if (!empty($r->batch_name)) {
        $batch_name = (string)($r->batch_name);
        $batch_name = BoincDb::escape_string($batch_name);
        $batch = BoincBatch::lookup_name($batch_name);
    } else {
        xml_error(-1, "batch not specified");
    }
    if (!$batch) xml_error(-1, "no such batch");
    return $batch;
}

function query_batch($r) {
    xml_start_tag("query_batch");
    list($user, $user_submit) = authenticate_user($r, null);
    $batch = get_batch($r);
    if ($batch->user_id != $user->id) {
        xml_error(-1, "not owner of batch");
    }

    $wus = BoincWorkunit::enum("batch = $batch->id", "order by id");
    $batch = get_batch_params($batch, $wus);
    $get_cpu_time = (int)($r->get_cpu_time);
    $get_job_details = (int)($r->get_job_details);
    print_batch_params($batch, $get_cpu_time);
    foreach ($wus as $wu) {
        echo "        <job>
        <id>$wu->id</id>
        <name>$wu->name</name>
        <canonical_instance_id>$wu->canonical_resultid</canonical_instance_id>
";
        // does anyone need this?
        //
        if (0) {
            $n_outfiles = n_outfiles($wu);
            echo "     <n_outfiles>$n_outfiles</n_outfiles>\n";
        }

        if ($get_job_details) {
            show_job_details($wu);
        }
        echo "        </job>\n";
    }
    echo "</query_batch>\n";
}

function results_sent($wu) {
    return BoincResult::count("workunitid=$wu->id and sent_time>0");
}

// variant for Condor, which doesn't care about job instances
// and refers to batches by name
//
function query_batch2($r) {
    xml_start_tag("query_batch2");
    list($user, $user_submit) = authenticate_user($r, null);
    $batch_names = $r->batch_name;
    $batches = array();
    foreach ($batch_names as $b) {
        $batch_name = (string)$b;
        $batch_name = BoincDb::escape_string($batch_name);
        $batch = BoincBatch::lookup_name($batch_name);
        if (!$batch) {
            xml_error(-1, "no batch named $batch_name");
        }
        if ($batch->user_id != $user->id) {
            xml_error(-1, "not owner of $batch_name");
        }
        $batches[] = $batch;
    }

    $min_mod_time = (double)$r->min_mod_time;
    if ($min_mod_time) {
        $mod_time_clause = "and mod_time > FROM_UNIXTIME($min_mod_time)";
    } else {
        $mod_time_clause = "";
    }

    $t = dtime();
    echo "<server_time>$t</server_time>\n";
    foreach ($batches as $batch) {
        $wus = BoincWorkunit::enum("batch = $batch->id $mod_time_clause");
        echo "   <batch_size>".count($wus)."</batch_size>\n";

        // job status is:
        // DONE if done
        // ERROR if error
        // IN_PROGRESS if at least one instance sent
        // QUEUED if no instances sent
        foreach ($wus as $wu) {
            if ($wu->canonical_resultid) {
                $status = "DONE";
            } else if ($wu->error_mask) {
                $status = "ERROR";
            } else if (results_sent($wu) > 0) {
                $status = "IN_PROGRESS";
            } else {
                $status = "UNSENT";
            }
            echo
"    <job>
        <job_name>$wu->name</job_name>
        <status>$status</status>
    </job>
";
        }
    }
    echo "</query_batch2>\n";
}

function query_job($r) {
    xml_start_tag("query_job");
    list($user, $user_submit) = authenticate_user($r, null);
    $job_id = (int)($r->job_id);
    $wu = BoincWorkunit::lookup_id($job_id);
    if (!$wu) xml_error(-1, "no such job");
    $batch = BoincBatch::lookup_id($wu->batch);
    if ($batch->user_id != $user->id) {
        xml_error(-1, "not owner");
    }
    $results = BoincResult::enum("workunitid=$job_id");
    foreach ($results as $result) {
        echo "    <instance>
        <name>$result->name</name>
        <id>$result->id</id>
        <state>".state_string($result)."</state>
";
        if ($result->server_state == 5) {   // over?
            $paths = get_outfile_paths($result);
            foreach($paths as $path) {
                if (is_file($path)) {
                    $size = filesize($path);
                    echo "        <outfile>
            <size>$size</size>
        </outfile>
";
                }
            }
        }
        echo "</instance>\n";
    }
    echo "</query_job>\n";
}

// the following for Condor.
// If the job has a canonical instance, return info about it.
// Otherwise find an instance that completed
// (possibly crashed) and return its info.
//
function query_completed_job($r) {
    xml_start_tag("query_completed_job");
    list($user, $user_submit) = authenticate_user($r, null);
    $job_name = (string)($r->job_name);
    $job_name = BoincDb::escape_string($job_name);
    $wu = BoincWorkunit::lookup("name='$job_name'");
    if (!$wu) xml_error(-1, "no such job");
    $batch = BoincBatch::lookup_id($wu->batch);
    if ($batch->user_id != $user->id) {
        xml_error(-1, "not owner");
    }

    echo "<completed_job>\n";
    echo "   <error_mask>$wu->error_mask</error_mask>\n";
    if ($wu->canonical_resultid) {
        $result = BoincResult::lookup_id($wu->canonical_resultid);
        echo "   <canonical_resultid>$wu->canonical_resultid</canonical_resultid>\n";
    } else {
        $results = BoincResult::enum("workunitid=$job_id");
        foreach ($results as $r) {
            switch($r->outcome) {
            case 1:
            case 3:
            case 6:
                $result = $r;
                break;
            }
        }
        if ($result) {
            echo "   <error_resultid>$result->id</error_resultid>\n";
        }
    }
    if ($result) {
        echo "   <exit_status>$result->exit_status</exit_status>\n";
        echo "   <elapsed_time>$result->elapsed_time</elapsed_time>\n";
        echo "   <cpu_time>$result->cpu_time</cpu_time>\n";
        echo "   <stderr_out><![CDATA[\n";
        echo htmlspecialchars($result->stderr_out);
        echo "   ]]></stderr_out>\n";
    }
    echo "</completed_job>
        </query_completed_job>
    ";
}

function handle_abort_batch($r) {
    xml_start_tag("abort_batch");
    list($user, $user_submit) = authenticate_user($r, null);
    $batch = get_batch($r);
    if ($batch->user_id != $user->id) {
        xml_error(-1, "not owner");
    }
    abort_batch($batch);
    echo "<success>1</success>
        </abort_batch>
    ";
}

// handle the abort of jobs possibly belonging to different batches
//
function handle_abort_jobs($r) {
    xml_start_tag("abort_jobs");
    list($user, $user_submit) = authenticate_user($r, null);
    $batch = null;
    foreach ($r->job_name as $job_name) {
        $job_name = BoincDb::escape_string($job_name);
        $wu = BoincWorkunit::lookup("name='$job_name'");
        if (!$wu) {
            xml_error(-1, "no job $job_name");
        }
        if (!$wu->batch) {
            xml_error(-1, "job $job_name is not part of a batch");
        }
        if (!$batch || $wu->batch != $batch->id) {
            $batch = BoincBatch::lookup_id($wu->batch);
        }
        if (!$batch || $batch->user_id != $user->id) {
            xml_error(-1, "not owner of batch");
        }
        echo "<aborted $job_name>\n";
        abort_workunit($wu);
    }
    echo "<success>1</success>
        </abort_jobs>
    ";
}

function handle_retire_batch($r) {
    xml_start_tag("retire_batch");
    list($user, $user_submit) = authenticate_user($r, null);
    $batch = get_batch($r);
    if ($batch->user_id != $user->id) {
        xml_error(-1, "not owner of batch");
    }
    retire_batch($batch);
    echo "<success>1</success>
        </retire_batch>
    ";
}

function handle_set_expire_time($r) {
    xml_start_tag("set_expire_time");
    list($user, $user_submit) = authenticate_user($r, null);
    $batch = get_batch($r);
    if ($batch->user_id != $user->id) {
        xml_error(-1, "not owner of batch");
    }
    $expire_time = (double)($r->expire_time);
    if ($batch->update("expire_time=$expire_time")) {
        echo "<success>1</success>";
    } else {
        xml_error(-1, "batch update failed");
    }
    echo "</set_expire_time>\n";
}

function get_templates($r) {
    xml_start_tag("get_templates");
    $app_name = (string)($r->app_name);
    if ($app_name) {
        $app = get_submit_app($app_name);
    } else {
        $job_name = (string)($r->job_name);
        $wu = get_wu($job_name);
        $app = BoincApp::lookup_id($wu->appid);
    }

    list($user, $user_submit) = authenticate_user($r, $app);
    $in = file_get_contents(project_dir() . "/templates/".$app->name."_in");
    $out = file_get_contents(project_dir() . "/templates/".$app->name."_out");
    if ($in === false || $out === false) {
        xml_error(-1, "template file missing");
    }
    echo "<templates>\n$in\n$out\n</templates>
        </get_templates>
    ";
}

function ping($r) {
    xml_start_tag("ping");
    BoincDb::get();     // errors out if DB down or web disabled
    echo "<success>1</success>
        </ping>
    ";
}

if (0) {
$r = simplexml_load_string("
<query_batch2>
    <authenticator>x</authenticator>
    <batch_name>batch_30</batch_name>
    <batch_name>batch_31</batch_name>
</query_batch2>
");
query_batch2($r);
exit;
}

if (0) {
$r = simplexml_load_string("
<query_batch>
    <authenticator>x</authenticator>
    <batch_id>54</batch_id>
</query_batch>
");
query_batch($r);
exit;
}

if (0) {
$r = simplexml_load_string("
<query_job>
    <authenticator>x</authenticator>
    <job_id>312173</job_id>
</query_job>
");
query_job($r);
exit;
}

if (0) {
$r = simplexml_load_string("
<estimate_batch>
    <authenticator>x</authenticator>
    <batch>
    <app_name>remote_test</app_name>
    <batch_name>Aug 6 batch 4</batch_name>
    <job>
        <rsc_fpops_est>19000000000</rsc_fpops_est>
        <command_line>--t 19</command_line>
        <input_file>
            <mode>remote</mode>
            <source>https://google.com/</source>
        </input_file>
    </job>
    </batch>
</estimate_batch>
");
estimate_batch($r);
exit;
}

$request_log = parse_config(get_config(), "<remote_submission_log>");
if ($request_log) {
    $request_log_dir = parse_config(get_config(), "<log_dir>");
    if ($request_log_dir) {
        $request_log = $request_log_dir . "/" . $request_log;
    }
    if ($file = fopen($request_log, "a+")) {
        fwrite($file, "\n<submit_rpc_handler date=\"" . date(DATE_ATOM) . "\">\n" . $_POST['request'] . "\n</submit_rpc_handler>\n");
        fclose($file);
    }
}

xml_header();
if (0) {
    $req = file_get_contents("submit_req.xml");
} else {
    $req = $_POST['request'];
}
$r = simplexml_load_string($req);
if (!$r) {
    xml_error(-1, "can't parse request message: $req");
}

switch ($r->getName()) {
    case 'abort_batch': handle_abort_batch($r); break;
    case 'abort_jobs': handle_abort_jobs($r); break;
    case 'create_batch': create_batch($r); break;
    case 'estimate_batch': estimate_batch($r); break;
    case 'get_templates': get_templates($r); break;
    case 'ping': ping($r); break;
    case 'query_batch': query_batch($r); break;
    case 'query_batch2': query_batch2($r); break;
    case 'query_batches': query_batches($r); break;
    case 'query_job': query_job($r); break;
    case 'query_completed_job': query_completed_job($r); break;
    case 'retire_batch': handle_retire_batch($r); break;
    case 'set_expire_time': handle_set_expire_time($r); break;
    case 'submit_batch': submit_batch($r); break;
    default: xml_error(-1, "bad command: ".$r->getName());
}

?>
