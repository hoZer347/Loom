#pragma once

/// hoZer's Utilities: state machines, attributes, dialogue and sprite animation.
/// Umbrella header; include this to get the whole library. Everything lives in
/// namespace Loom.
///
///	  State machines       StateMachineBase is the pump: a queue, a current state,
///	                       parallel sub-states, and a history for debugging. Pick the
///	                       machine that matches the owner. StateMachine<Self> sits on a
///	                       GameObject and is self-typed, since Component is CRTP.
///	                       StaticStateMachine is always on and owned by nothing.
///	                       States are State<Self, Focus>.
///
///	  Attributes           Attribute<T, CacheMode> with its modifiers and triggers. An
///	                       owner declares its attributes once with HOZER_ATTRIBUTES,
///	                       beside the fields themselves, which is what AttributeFields
///	                       walks.
///
///	  Dialogue             A text script is tokenised and compiled into states. Bindings
///	                       are lambdas whose signatures declare what they take. Both the
///	                       line text and the voice blip are announced rather than rendered
///	                       or played; the caller decides what to do with them.
///
///	  Sprites              SpriteSheetScanner reads a raw RGBA buffer. SpriteManager is a
///	                       Component drawing the shared unit quad through Flipbook.shader
///	                       and carrying its own position instead of its GameObject's transform.
///
///	  Timing               Duration, Gait and SpeedManager, measured against Clock.
///
///	  Tooling              ImGui windows over the running game: StateMachineMonitor for
///	                       machines and their inspector, AttributeGui for attributes,
///	                       AxisGui for vectors edged in their axis colours.

#include "Attributes.h"
#include "AttributeFields.h"
#include "AttributeGui.h"
#include "AttributeTriggers.h"
#include "AxisGui.h"
#include "CustomState.h"
#include "DialogueArgs.h"
#include "DialogueInput.h"
#include "DialogueManager.h"
#include "DialogueStates.h"
#include "Duration.h"
#include "Gait.h"
#include "Modifiers.h"
#include "SpeedManager.h"
#include "SpriteManager.h"
#include "SpriteSheetScanner.h"
#include "State.h"
#include "StateMachine.h"
#include "StateMachineMonitor.h"
#include "StateReference.h"
#include "StaticStateMachine.h"
#include "Clock.h"
