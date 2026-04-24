// Copyright 2017-2019 Crytek GmbH / Crytek Group. All rights reserved.
#pragma once

#include <CryEntitySystem/IEntityComponent.h>
#include <CryMath/Cry_Camera.h>

#include <ICryMannequin.h>

#include <DefaultComponents/Cameras/CameraComponent.h>
#include <DefaultComponents/Physics/CharacterControllerComponent.h>
#include <DefaultComponents/Geometry/AdvancedAnimationComponent.h>
#include <DefaultComponents/Input/InputComponent.h>
#include <DefaultComponents/Audio/ListenerComponent.h>
#include <CryAISystem/Components/IEntityNavigationComponent.h>

////////////////////////////////////////////////////////
// Represents a player participating in gameplay
////////////////////////////////////////////////////////
class CPlayerComponent final : public IEntityComponent
{
public:
	CPlayerComponent() = default;
	virtual ~CPlayerComponent() = default;

	// IEntityComponent
	virtual void Initialize() override;

	virtual Cry::Entity::EventFlags GetEventMask() const override;
	virtual void ProcessEvent(const SEntityEvent& event) override;
	// ~IEntityComponent

	// Reflect type to set a unique identifier for this component
	static void ReflectType(Schematyc::CTypeDesc<CPlayerComponent>& desc)
	{
		desc.SetGUID("{63F4C0C6-32AF-4ACB-8FB0-57D45DD14725}"_cry_guid);
		desc.AddMember(&CPlayerComponent::m_viewDistanceFromPlayer, 'vdst', "ViewDistanceFromPlayer", "View Distance From Player", "View distance from the player.", 10.0f);
	}

	void OnReadyForGameplayOnServer();
	bool IsLocalClient() const { return (m_pEntity->GetFlags() & ENTITY_FLAG_LOCAL_PLAYER) != 0; }
	
protected:
	void Revive(const Matrix34& transform);
	
	void UpdateAnimation(float frameTime);
	void UpdateCamera(float frameTime);

	void UpdateCursor(float frameTime);

	void SpawnCursorEntity();
	
	// Called when this entity becomes the local player, to create client specific setup such as the Camera
	void InitializeLocalPlayer();

	// Start remote method declarations
protected:
	// Parameters to be passed to the RemoteReviveOnClient function
	struct RemoteReviveParams
	{
		// Called once on the server to serialize data to the other clients
		// Then called once on the other side to deserialize
		void SerializeWith(TSerialize ser)
		{
			// Serialize the position with the 'wrld' compression policy
			ser.Value("pos", position, 'wrld');
			// Serialize the rotation with the 'ori0' compression policy
			ser.Value("rot", rotation, 'ori0');
		}
		
		Vec3 position;
		Quat rotation;
	};
	// Remote method intended to be called on all remote clients when a player spawns on the server
	bool RemoteReviveOnClient(RemoteReviveParams&& params, INetChannel* pNetChannel);
	
	struct RemoteShootParams
	{
		Vec3 position;
		Quat rotation;

		void SerializeWith(TSerialize ser)
		{
			ser.Value("pos", position, 'wrld');
			ser.Value("rot", rotation, 'ori0');
		}
	};

	bool RemoteShootOnServer(RemoteShootParams&& params, INetChannel* pNetChannel);

protected:
	bool m_isAlive = false;

	Cry::DefaultComponents::CCameraComponent* m_pCameraComponent = nullptr;
	Cry::DefaultComponents::CCharacterControllerComponent* m_pCharacterController = nullptr;
	Cry::DefaultComponents::CAdvancedAnimationComponent* m_pAnimationComponent = nullptr;
	Cry::DefaultComponents::CInputComponent* m_pInputComponent = nullptr;
	Cry::Audio::DefaultComponents::CListenerComponent* m_pAudioListenerComponent = nullptr;
	IEntityNavigationComponent* m_pNavigationComponent = nullptr;

	FragmentID m_idleFragmentId;
	FragmentID m_walkFragmentId;
	FragmentID m_activeFragmentId;

	float m_movementSpeed = 5.0f;
	float m_jumpHeight = 5.0f;

	float m_viewDistanceFromPlayer = 10.0f;

	IEntity* m_pCursorEntity = nullptr;

public:
	void Jump();
	void Shoot();

	bool IsSwimming();

	struct SInitializeLocalPlayer
	{
		SInitializeLocalPlayer() = default;
	};
};
