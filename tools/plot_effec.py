#!/usr/bin/env python3

# Uncomment this for generating PDF without a X server
#import matplotlib as mpl
#mpl.use('Agg')

import matplotlib.pyplot as plt
from pprint import pprint
from scipy.interpolate import spline
import sys
import numpy
import os
import math

if len(sys.argv) <= 2:
	sys.exit('\nUsage:\n\tpython3 "' + sys.argv[0] + '" <base_time_log> <time_log1> [<time_log2> ...]\n')

def ParseBaseTimeLog(FileName):
	Workload = []
	List = []
	BaseTime = 0.0
	with open(FileName) as Fin:
		for Line in Fin:
			if Line[0] == '#':
				continue
			(NumWorkload, Time) = Line.split('\t')
			Workload.append(int(NumWorkload.strip()))
			if (len(List) == 0):
				BaseTime = float(Time.strip())
				List.append(float(1.0))
				sys.stdout.write(" %6.2f" % 1.0)
			else:
				NTime = float(Time.strip()) / BaseTime
				List.append(NTime)
				sys.stdout.write(" %6.2f" % NTime)
	if len(Workload) == 0:
		sys.exit("no bench found")
	sys.stdout.write('\n')
	return (Workload, List, BaseTime)

def ParseTimeLog(FileName, BaseTime):
	List = []
	with open(FileName) as Fin:
		for Line in Fin:
			if Line[0] == '#':
				continue
			(NumWorkload, Time) = Line.split('\t')
			NTime = float(Time.strip()) / BaseTime
			List.append(NTime)
			sys.stdout.write(" %6.2f" % NTime)
	sys.stdout.write('\n')
	return List

sys.stdout.write("%11s |" % 'Base')
BaseTimeLog = ParseBaseTimeLog(sys.argv[1])
TimeLog = []
Index = numpy.arange(len(BaseTimeLog[0]))

plt.rcParams.update({'font.size': 12.5})

Figure = plt.figure(figsize=(16,5))

Axe = Figure.add_subplot(111)
Axe.set_xlabel("Number of Low Priority Workload")
Axe.set_ylabel("Normalized Execution Time")

#XNew = numpy.linspace(BaseTimeLog[0][0], BaseTimeLog[0][-1], 300)
#YNew = spline(BaseTimeLog[0], BaseTimeLog[1], XNew)
plt.plot(BaseTimeLog[0], BaseTimeLog[1], marker='o', label='Base')

for ArgIdx in range(2, len(sys.argv)):
	ConfigName = os.path.splitext(os.path.basename(sys.argv[ArgIdx]))[0]
	sys.stdout.write("%11s |" % ConfigName)
	NewTimeLog = ParseTimeLog(sys.argv[ArgIdx], BaseTimeLog[2])
	plt.plot(BaseTimeLog[0], NewTimeLog, label=ConfigName, marker='x')

Axe.legend(loc=2)
plt.tight_layout()

plt.plot()
Axe.set_ylim(ymin=0)
Axe.grid('on', ls=':')

#plt.show()
plt.savefig("plot.pdf")
