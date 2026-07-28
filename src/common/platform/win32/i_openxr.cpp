#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "i_input.h"



#include "common/rendering/rt/rt_openxr_input.h"

namespace
{
	constexpr float DEFAULT_DEADZONE = 0.25f;

	enum Hand
	{
		ON,
		OFF
	};

	enum Source
	{
		STICK,
		PAD
	};

	enum Axis
	{
		X,
		Y
	};


	const EJoyAxis DefaultMap[4] = { JOYAXIS_Side, JOYAXIS_Forward, JOYAXIS_None, JOYAXIS_None };

	class FOpenXRJoystick : public IJoystickConfig
	{
	public:
		FOpenXRJoystick()
		{
			SetDefaultConfig();
			Multiplier = 1.0f;
			M_LoadJoystickConfig(this);
			Apply();
		}

		~FOpenXRJoystick()
		{
			M_SaveJoystickConfig(this);
		}

		FString GetName() override { return "OpenXR"; }
		float GetSensitivity() override { return Multiplier; }
		void SetSensitivity(float scale) override { Multiplier = scale; Apply(); }
		int GetNumAxes() override { return 4; }

		float GetAxisDeadZone(int axis) override
		{
			return unsigned(axis) < 4 ? Axes[axis].DeadZone : 0.0f;
		}

		EJoyAxis GetAxisMap(int axis) override
		{
			return unsigned(axis) < 4 ? Axes[axis].GameAxis : JOYAXIS_None;
		}

		const char* GetAxisName(int axis) override
		{
			FString& name = Axes[axis].Name;
			name = "";
			name = axis == 0 ? "Left Thumbstick Horizontal" : axis == 1 ? "Left Thumbstick Vertical" : axis == 2 ? "Right Thumbstick Horizontal" : "Right Thumbstick Vertical";
			return name.GetChars();
		}

		float GetAxisScale(int axis) override
		{
			return unsigned(axis) < 4 ? Axes[axis].Multiplier : 0.0f;
		}

		void SetAxisDeadZone(int axis, float v) override
		{
			if (unsigned(axis) < 4) { Axes[axis].DeadZone = v; Apply(); }
		}

		void SetAxisMap(int axis, EJoyAxis map) override
		{
			if (unsigned(axis) < 4) { Axes[axis].GameAxis = map; Apply(); }
		}

		void SetAxisScale(int axis, float v) override
		{
			if (unsigned(axis) < 4) { Axes[axis].Multiplier = v; Apply(); }
		}

		bool GetEnabled() override { return true; }
		void SetEnabled(bool enabled) override { (void)enabled; }
		bool AllowsEnabledInBackground() override { return true; }
		bool GetEnabledInBackground() override { return true; }
		void SetEnabledInBackground(bool enabled) override { (void)enabled; }
		bool IsSensitivityDefault() override { return Multiplier == 1.0f; }
		bool IsAxisDeadZoneDefault(int axis) override { return Axes[axis].DeadZone == DEFAULT_DEADZONE; }
		bool IsAxisMapDefault(int axis) override { return Axes[axis].GameAxis == DefaultMap[axis]; }
		bool IsAxisScaleDefault(int axis) override { return Axes[axis].Multiplier == 1.0f; }

		void SetDefaultConfig() override
		{
			for (int i = 0; i < 4; ++i)
			{
				Axes[i].GameAxis = DefaultMap[i];
				Axes[i].DeadZone = DEFAULT_DEADZONE;
				Axes[i].Multiplier = 1.0f;
			}
		}

		FString GetIdentifier() override { return "OpenXR"; }

		void Apply() { float d[4], s[4]; EJoyAxis m[4]; for (int i=0;i<4;++i) { d[i]=Axes[i].DeadZone; s[i]=Axes[i].Multiplier; m[i]=Axes[i].GameAxis; } RT_OpenXRInputConfigure(d,s,m,Multiplier); }

	private:
		struct AxisInfo
		{
			float Multiplier;
			float DeadZone;
			EJoyAxis GameAxis;
			FString Name;
		};

		float Multiplier = 1.0f;
		AxisInfo Axes[4];
	};

	class FOpenXRJoystickManager : public FJoystickCollection
	{
	public:
		bool GetDevice() override
		{
			return true;
		}

		void AddAxes(float axes[NUM_JOYAXIS]) override
		{
			(void)axes;
		}

		void GetDevices(TArray<IJoystickConfig*>& sticks) override
		{
			if (RT_OpenXRInputAvailable())
			{
				sticks.Push(&mDevice);
			}
		}

		IJoystickConfig* Rescan() override
		{
			return RT_OpenXRInputAvailable() ? &mDevice : nullptr;
		}

	private:
		FOpenXRJoystick mDevice;
	};
}

void I_StartupOpenXR()
{
	if (JoyDevices[INPUT_OpenXR] == NULL)
	{
		JoyDevices[INPUT_OpenXR] = new FOpenXRJoystickManager;
	}
}

