# LCD Message Flow (2x16 LCD)

State transition design for a 2-row x 16-column LCD messaging UI, using a keypad where numeric keys are dedicated to text entry (multi-tap style) and non-numeric keys are used for navigation/actions.

```mermaid
stateDiagram-v2
		direction LR
		[*] --> Initial

		state "Incoming Message" as Initial
		state "Menu" as Menu
		state "Create Message" as Compose
		state "Error" as Error

		note right of Initial
			Shows incoming messages and alerts.
			Press A to open Menu.
			* and # navigate message list.
			B and C pan truncated text.
		end note

		note right of Menu
			A returns to Incoming Message.
			B enters Create Message.
		end note

		note right of Compose
			0-9 uses multi-tap text entry.
			B commits current character.
			D removes trailing character.
			C sends message.
			A returns to Menu.
		end note

		note right of Error
			Entered on invalid input or send/runtime failure.
			Acknowledgement returns to prior state.
		end note

		Initial --> Initial: *
		Initial --> Initial: #
		Initial --> Initial: B
		Initial --> Initial: C
		Initial --> Menu: A

		Menu --> Initial: A
		Menu --> Compose: B

		Compose --> Compose: 0-9
		Compose --> Compose: B
		Compose --> Compose: D
		Compose --> Initial: C
		Compose --> Menu: A

		Initial --> Error: SYS
		Menu --> Error: SYS
		Compose --> Error: SYS

		Error --> Initial: ACK
		Error --> Menu: ACK
		Error --> Compose: ACK
```

## Keypad Input Mapping

- 0-9: character input (multi-tap phone-style text entry)
- A: menu and back transitions (also error acknowledge)
- B: select/confirm and horizontal text scroll (left)
- C: send action and horizontal text scroll (right)
- D: delete last character in compose state
- *: previous message in incoming message view
- #: next message in incoming message view
