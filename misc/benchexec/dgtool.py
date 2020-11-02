# This file is part of BenchExec, a framework for reliable benchmarking:
# https://github.com/sosy-lab/benchexec
#
# SPDX-FileCopyrightText: 2007-2020 Dirk Beyer <https://www.sosy-lab.org>
# SPDX-FileCopyrightText: 2020 Marek Chalupa <https://www.sosy-lab.org>
#
# SPDX-License-Identifier: Apache-2.0

from os.path import basename

import benchexec.util as util
import benchexec.tools.template
import benchexec.result as result

class Tool(benchexec.tools.template.BaseTool):
    """
    A benchexec module for running dgtool with given tool
    on benchmarks
    """

    def executable(self):
        return util.find_executable("dgtool")

    def version(self, executable):
        return self._version_from_tool(executable)

    def name(self):
        return "dgtool"

    def cmdline(self, executable, options, tasks, propertyfile, rlimits):
        self.tool = basename(options[0])
        return [executable] + options + ['-Xdg', 'dbg'] + tasks

    def get_phase(self, tool, output):
        phase='start'
        for line in output:
            if '> clang' in line:
                phase='clang'
            elif '> opt' in line:
                phase='opt'
            elif '> llvm-link' in line:
                phase='llvm-link'
            elif tool in line:
                phase=tool
        return phase
                
    def determine_result(self, returncode, returnsignal, output, isTimeout):

        if isTimeout:
            return f"{self.get_phase(self.tool, output)}"

        if returnsignal == 0 and returncode == 0:
            return result.RESULT_DONE
        return f"{result.RESULT_ERROR}({self.get_phase(self.tool, output)})"
