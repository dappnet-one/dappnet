# -*- coding: utf-8 -*-

import subprocess
import logging
import os
import time
import sys
import json
import datetime

from .dpn_tools import *

LOG_LEVEL = logging.INFO
LOG_FORMAT = "%(asctime)-15s - %(name)s - %(levelname)s - %(message)s"
MAIN_LOG_PATH = "./dpn_runner.log"

MODULE_NAME = "SPS-Tester-via-dpnpy.Runner-Py"
logger = logging.getLogger(MODULE_NAME)
logger.setLevel(LOG_LEVEL)


class DpnNode(object):
    def __init__(self, dpn_executable, working_dir, config_src_path):
        self.dpn_executable = dpn_executable
        self.working_dir = working_dir
        self.config_src_path = config_src_path

        from shutil import rmtree, copy
        # remove old data from node
        if os.path.exists(self.working_dir):
            rmtree(self.working_dir)
        os.makedirs(self.working_dir+"/blockchain")
        # copy config file to working dir
        copy(self.config_src_path, self.working_dir + "/config.ini")

        self.dpn_config = self.parse_node_config_file(self.working_dir + "/config.ini")
        self.ip_address, self.port = self.dpn_config["webserver-http-endpoint"][0].split(":")
        self.ip_address = "http://{}".format(self.ip_address)
        self.node_running = False

    def get_from_config(self, key):
        return self.dpn_config.get(key, None)

    def get_node_url(self):
        return "{}:{}/".format(self.ip_address, self.port)

    def is_running(self):
        return self.node_running

    def parse_node_config_file(self, config_file_name):
        ret = dict()
        lines = None
        with open(config_file_name, "r") as f:
            lines = f.readlines()

        for line in lines:
            proc_line = line.strip()
            if proc_line:
                if proc_line.startswith("#"):
                    continue
                k, v = proc_line.split("=", 1)
                k = k.strip()
                v = v.strip()
                if k in ret:
                    ret[k].append(v)
                else:
                    ret[k] = [v]
        return ret

    def run_dpn_node(self, additional_params = [], wait_for_blocks = True):
        detect_process_by_name("dpn", self.ip_address, self.port)

        logger.info("*** START NODE at {0}:{1} in {2}".format(self.ip_address, self.port, self.working_dir))

        parameters = [
            self.dpn_executable,
            "-d",
            self.working_dir,
            "--advanced-benchmark",
            "--sps-remove-threshold",
            "-1"
        ]

        parameters = parameters + additional_params
        
        self.pid_file_name = "{0}/run_dpn-{1}.pid".format(self.working_dir, self.port)
        current_time_str = datetime.datetime.now().strftime("%Y-%m-%d")
        log_file_name = "{0}/{1}-{2}-{3}.log".format(self.working_dir, "dpn", self.port, current_time_str)
        screen_cfg_name = "{0}/dpn_screen-{1}.cfg".format(self.working_dir, self.port)

        save_screen_cfg(screen_cfg_name, log_file_name)
        screen_params = [
            "screen",
            "-m",
            "-d",
            "-L",
            "-c",
            screen_cfg_name,
            "-S",
            "{0}-{1}-{2}".format("dpn", self.port, current_time_str)
        ]

        parameters = screen_params + parameters
        logger.info("Running dpnd with command: {0}".format(" ".join(parameters)))
        
        try:
            subprocess.Popen(parameters)
            save_pid_file(self.pid_file_name, "dpn", self.port, current_time_str)
            if wait_for_blocks:
                wait_for_blocks_produced(2, "{}:{}".format(self.ip_address, self.port))
            else:
                wait_for_string_in_file(log_file_name, "start listening for ws requests", 240.)
            self.node_running = True
            logger.info("Node at {0}:{1} in {2} is up and running...".format(self.ip_address, self.port, self.working_dir))
        except Exception as ex:
            logger.error("Exception during dpnd run: {0}".format(ex))
            kill_process(self.pid_file_name, "dpn", self.ip_address, self.port)
            self.node_running = False


    def stop_dpn_node(self):
        logger.info("Stopping node at {0}:{1}".format(self.ip_address, self.port))
        kill_process(self.pid_file_name, "dpn", self.ip_address, self.port)
        self.node_running = False


if __name__ == "__main__":
    node = DpnNode("/home/dariusz-work/Builds/dpn/programs/dpnd/dpnd", "/home/dariusz-work/dpn-data", "./resources/config.ini.in")
    node.run_dpn_node()
    node.stop_dpn_node()
