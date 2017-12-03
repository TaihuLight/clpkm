#!/usr/bin/env python3

import matplotlib.pyplot as plt
from pprint import pprint
import sys
import numpy
import os
import math

if len(sys.argv) <= 2:
	sys.exit('\nUsage:\n\tpython3 "' + sys.argv[0] + '" <base_time_log> <time_log1> [<time_log2> ...]\n')

def ParseBaseTimeLog(FileName):
	BenchName = []
	List = []
	with open(FileName) as Fin:
		for Line in Fin:
			(Time, Bench) = Line.split('\t')
			BenchName.append(Bench.strip())
			List.append(float(Time))
	if len(BenchName) == 0:
		sys.exit("no bench found")
	BenchName.append("G-Mean")
	return (BenchName, List)

def ParseTimeLog(FileName):
	List = []
	with open(FileName) as Fin:
		for Line in Fin:
			(Time, Bench) = Line.split('\t')
			List.append(float(Time))
	return List

def NormalizeTimeList(Log, BaseLog):
	if len(BaseLog) != len(Log):
		sys.exit("List length mismatch")
	Count = 0
	GMean = 1.0
	for BenchIdx in range(0, len(BaseLog)):
		Log[BenchIdx] = Log[BenchIdx] / BaseLog[BenchIdx]
		if Log[BenchIdx] > 0:
			Count += 1
			GMean *= Log[BenchIdx]
	Log.append(math.pow(GMean, 1.0 / Count))

BaseTimeLog = ParseBaseTimeLog(sys.argv[1])
TimeLog = []
Index = numpy.arange(len(BaseTimeLog[0]))

Figure = plt.figure()
Axe = Figure.add_subplot(111)
Axe.set_xlabel("Benchmarks")
Axe.set_ylabel("Normalized Execution Time")

Width = 0.25
AccWidth = 0

Bar = ()
BarName = ()

for ArgIdx in range(2, len(sys.argv)):
	NewTimeLog = ParseTimeLog(sys.argv[ArgIdx])
	NormalizeTimeList(NewTimeLog, BaseTimeLog[1])
	Rect = Axe.bar(Index + AccWidth, NewTimeLog, Width)
	Bar = Bar + (Rect[0], )
	BarName = BarName + (os.path.basename(os.path.dirname(sys.argv[ArgIdx])), )
	AccWidth += Width

plt.tight_layout()
Axe.set_xticks(Index + ((len(sys.argv) - 3) * Width) / 2)
Axe.set_xticklabels(BaseTimeLog[0])

plt.legend(Bar, BarName)

#pprint(TimeLog)
#sys.exit(":)")

plt.plot()
#plt.show()
plt.savefig("plot.svg")
