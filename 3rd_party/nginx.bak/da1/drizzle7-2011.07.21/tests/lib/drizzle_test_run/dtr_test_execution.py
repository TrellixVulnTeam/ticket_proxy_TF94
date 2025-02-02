#! /usr/bin/env python
# -*- mode: python; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
#
# Copyright (C) 2010 Patrick Crews
#
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

""" dtr_test_execution:
    code related to the execution of dtr test cases 
    
    We are provided access to a testManager with 
    dtr-specific testCases.  We contact teh executionManager
    to produce the system and server configurations we need
    to execute a test.

"""

# imports
import os
import sys
import subprocess
import commands

import lib.test_mgmt.test_execution as test_execution

class testExecutor(test_execution.testExecutor):
    """ dtr-specific testExecutor 
        We currently execute by sending test-case
        data to client/drizzletest...for now

    """
  
    def execute_testCase (self):
        """ Execute a dtr testCase via calls to drizzletest (boo)
            Eventually, we will replace drizzletest with pythonic
            goodness, but we have these classes stored here for the moment

        """
        test_execution.testExecutor.execute_testCase(self)
        self.status = 0

        # generate command line
        drizzletest_cmd = self.generate_drizzletest_call()

        # call drizzletest
        self.execute_drizzletest(drizzletest_cmd)

        # analyze results
        self.current_test_status = self.process_drizzletest_output()
        self.set_server_status(self.current_test_status)
 

    def generate_drizzletest_call(self):
        """ Produce the command line we use to call drizzletest
            We have a healthy number of values, so we put this in a 
            nice function

        """

        drizzletest_arguments = [ '--no-defaults'
                                , '--silent'
                                , '--tmpdir=%s' %(self.master_server.tmpdir)
                                , '--logdir=%s' %(self.master_server.logdir)
                                , '--port=%d' %(self.master_server.master_port)
                                , '--database=test'
                                , '--user=root'
                                , '--password='
                                #, '--testdir=%s' %(self.test_manager.testdir)
                                , '--test-file=%s' %(self.current_testcase.testpath)
                                , '--tail-lines=20'
                                , '--timer-file=%s' %(self.master_server.timer_file)
                                , '--result-file=%s' %(self.current_testcase.resultpath)
                                ]
        if self.record_flag:
            # We want to record a new result
            drizzletest_arguments.append('--record')
        drizzletest_cmd = "%s %s %s" %( self.cmd_prefix
                                      , self.system_manager.code_tree.drizzletest
                                      , " ".join(drizzletest_arguments))
        return drizzletest_cmd

    def execute_drizzletest(self, drizzletest_cmd):
        """ Execute the commandline and return the result.
            We use subprocess as we can pass os.environ dicts and whatnot 

        """
        testcase_name = self.current_testcase.fullname
        self.time_manager.start(testcase_name,'test')
        #retcode, output = self.system_manager.execute_cmd( drizzletest_cmd
                                                 #         , must_pass = 0 )
        drizzletest_outfile = os.path.join(self.logdir,'drizzletest.out')
        drizzletest_output = open(drizzletest_outfile,'w')
        drizzletest_subproc = subprocess.Popen( drizzletest_cmd
                                         , shell=True
                                         , env=self.working_environment
                                         , stdout = drizzletest_output
                                         , stderr = subprocess.STDOUT
                                         )
        drizzletest_subproc.wait()
        retcode = drizzletest_subproc.returncode     
        execution_time = int(self.time_manager.stop(testcase_name)*1000) # millisec

        drizzletest_output.close()
        drizzletest_file = open(drizzletest_outfile,'r')
        output = ''.join(drizzletest_file.readlines())
        drizzletest_file.close()

        if self.debug:
            self.logging.debug("drizzletest_retcode: %d" %(retcode))
        self.current_test_retcode = retcode
        self.current_test_output = output
        self.current_test_exec_time = execution_time

    def process_drizzletest_output(self):
        """ Drizzletest has run, we now check out what we have """
        retcode = self.current_test_retcode
        if retcode == 0:
            return 'pass'
        elif retcode == 62 or retcode == 15872:
            return 'skipped'
        elif retcode == 63 or retcode == 1:
            return 'fail'
        else:
            return 'fail'

    def handle_system_reqs(self):
        """ We check our test case and see what we need to do
            system-wise to get ready.  This is likely to be 
            mode-dependent and this is just a placeholder
            method

        """

        self.process_environment_reqs()
        self.process_symlink_reqs()
        self.process_master_sh()  
        return

    def process_master_sh(self):
        """ We do what we need to if we have a master.sh file """
        if self.current_testcase.master_sh:
            retcode, output = self.system_manager.execute_cmd("/bin/sh %s" %(self.current_testcase.master_sh))
            if self.debug:
                self.logging.info("retcode: %retcode")
                self.logging.info("%output")

    def process_environment_reqs(self):
        """ We generate the ENV vars we need set
            and then ask systemManager to do so

        """
        env_reqs = { 'DRIZZLETEST_VARDIR': self.master_server.vardir
                   ,  'DRIZZLE_TMP_DIR': self.master_server.tmpdir
                   ,  'MASTER_MYSOCK': self.master_server.socket_file
                   ,  'MASTER_MYPORT': str(self.master_server.master_port)
                   ,  'MC_PORT': str(self.master_server.mc_port)
                   ,  'PBMS_PORT': str(self.master_server.pbms_port)
                   ,  'JSON_SERVER_PORT': str(self.master_server.json_server_port)
                   ,  'RABBITMQ_NODE_PORT': str(self.master_server.rabbitmq_node_port)
                   ,  'DRIZZLE_TCP_PORT': str(self.master_server.drizzle_tcp_port)
                   ,  'EXE_DRIZZLE': self.master_server.drizzle_client
                   ,  'MASTER_SERVER_SLAVE_CONFIG' : self.master_server.slave_config_file
                   ,  'DRIZZLE_DUMP': "%s --no-defaults -uroot -p%d" %( self.master_server.drizzledump
                                                        , self.master_server.master_port)
                   ,  'DRIZZLE_SLAP': "%s -uroot -p%d" %( self.master_server.drizzleslap
                                                        , self.master_server.master_port)
                   ,  'DRIZZLE_IMPORT': "%s -uroot -p%d" %( self.master_server.drizzleimport
                                                          , self.master_server.master_port)
                   ,  'DRIZZLE': "%s -uroot -p%d" %( self.master_server.drizzle_client
                                                   , self.master_server.master_port)
                   ,  'DRIZZLE_TRX_READER' : self.system_manager.code_tree.drizzle_trx_reader
                   }     

        self.working_environment = self.system_manager.create_working_environment(env_reqs)

    def process_symlink_reqs(self):
        """ Create any symlinks we may need """
        needed_symlinks = []

        self.system_manager.create_symlinks(needed_symlinks)

    
   

        
