#!/usr/bin/python
import uberbus.moodlamp
import uberbus.digitalinput
import time
import sys
import threading
import thread
import pygame
from pygame.mixer import music


lamps = sys.argv[1]
inputname = sys.argv[2]
lamp = sys.argv[3]

a = uberbus.moodlamp.Moodlamp(lamps,True)
di = uberbus.digitalinput.DigitalInput(inputname)
l = uberbus.moodlamp.Moodlamp(lamp)

state = 'b'
play = False
pygame.mixer.init()
#music.load('siren.wav')
music.load('puddi.mp3')
r = g = b = 0

def setcolors():
    global state, r, g, b
    global play
    while 1:
        if state == 'b0':
            a.timedfade(r,g,b,1)
            music.stop()
            play = False
            state = 'b1'
        elif state == 'b1':
            time.sleep(.1)
        elif state == 'r1':
            a.timedfade(255,0,0,.4)
            if not play:
            	music.play(-1)
                play = True
            state = 'r2'
            time.sleep(.4);
        elif state == 'r2':
            a.timedfade(0,0,0,.4)
            state = 'r1'
            time.sleep(.4)

thread.start_new_thread(setcolors,())
a.connect()
di.connect()
l.connect()

class DigitalInputCallback(uberbus.digitalinput.DigitalInputCallback):
    def onInput(self, node, pin, pinstate):
        global state, r, g, b
        if pin == 'alarm' and pinstate == 0:
            color = l.getcolor()
            r = ord(color[0])
            g = ord(color[1])
            b = ord(color[2])
            state = 'r1'
        elif pin == 'alarm' and pinstate == 1:
            state = 'b0'
            
di.listen(DigitalInputCallback())
di.checkForever()

