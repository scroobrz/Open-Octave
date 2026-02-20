---
trigger: always_on
---

# Open Octave Project

## Overview
**Open Octave** is a modular, robotically enhanced educational keyboard system designed to enhance piano learning in classrooms. The product will consist of octave-sized keyboard modules which can function independently or connect together to form larger keyboard components. Each module will be digitally and robotically augmented to facilitate accessible learning, with light-up keys and a self-pressing mechanism to effectively show beginners how to play melodies.

## The System
Open Octave reimagines the keyboard as a scalable system. Our modular hardware allows schools to start small and grow their inventory as budgets allow, while our robotic enhancements ensure the system is truly accessible to beginner learners - and our accompanying software makes the system manageable within a large classroom setting.

All Open Octave modules will support three modes:
1. Piano Mode, where the keyboard simply functions like a regular piano.
2. Guided Mode, where the keyboard lights up while being played to provide sequential fingering cues; the user plays along with the cues, and the keyboard waits for each key to be pressed before lighting up the next one.
3. Teaching Mode, where the keys press themselves down to physically demonstrate how to play a melody and key lights up with different colours indicating which finger should be used to press which key.

## Tasks
Open Octave is decomposed into three subsystems which require different skills and can be worked on in parallel by separate teams throughout the project: the hardware & mechanics, the firmware & electronics, and the software application. At a high level, the hardware & mechanics team is responsible for the physical manufacturing of the keys and interlocking keyboard frames, the firmware & electronics team is responsible for the electronic architecture and corresponding embedded programming, and the software team is responsible for developing the application which remotely controls the keyboards.

### Key Milestones
The project roadmap is centered around three key milestones:

**Demo 1 (11/2/2026): Robotic Fundamentals**
The goal of Demo 1 is to demonstrate the robotic aspect of Open Octave. We aim to have a few keys (at least two) connected to a microcontroller with speaker output, which can demonstrate the following basic robotic functions:
1. Pressing of keys triggering instantaneous audio output.
2. Robotic autopressing and lighting-up of keys; these functions should work in tandem, i.e should happen at the same time.
3. Automated playing of a basic, hardcoded melody using the autopressing and light-up functionality.

**Demo 2 (4/3/2026): Full Octave and Operating Modes**
The goal of Demo 2 is to demonstrate a framed 12-key octave which is able to showcase the basic functions of all three modes of operation. That is, the keyboard should demonstrate the following functionalities:
1. For Piano Mode: The keyboard should be fully functional as a basic keyboard, where a user can press keys and play music without any robotic assistance.
2. For Guided Mode: The keyboard should be able make keys light up in a specific sequence based on one or two preset hardcoded songs; the keyboard should light up one key at a time and wait until each key has been pressed before lighting up the next one; the light colours should correctly correspond to which fingers should be used to play each note.
3. For Teaching Mode: The keyboard should be able to automatically play a preset, hardcoded song in real time, utilising the auto-press and light-up functions to demonstrate how to play this song to the user.

**Demo 3 (25/3/2026): Modularity and Networking**
The goal of Demo 3 is to demonstrate the full vision of Open Octave: multiple keyboard modules physically connected and digitally networked to create a scalable infrastructure for learning. We aim to demonstrate the following:
1. Have two independently functional keyboard modules.
2. Have the keyboard modules physically interlock to form a fully-functional two-octave keyboard.
3. Have the larger two-octave keyboard showcase more complex songs than what is already possible on a single octave.
4. Have an external software application which can be used to remotely control the keyboard modules (on/off, volume, etc.), activate functions, configure modes, see module connection information, and maybe upload and select songs as MIDI files, and get feedback based on the students’ playing.