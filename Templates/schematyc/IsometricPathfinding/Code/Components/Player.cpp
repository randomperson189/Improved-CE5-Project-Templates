// Copyright 2017-2020 Crytek GmbH / Crytek Group. All rights reserved.
#include "StdAfx.h"
#include "Player.h"
#include "Bullet.h"
#include "SpawnPoint.h"
#include "GamePlugin.h"

#include <CryRenderer/IRenderAuxGeom.h>
#include <CryInput/IHardwareMouse.h>
#include <CrySchematyc/Env/Elements/EnvComponent.h>
#include <CrySchematyc/Env/Elements/EnvFunction.h>
#include <CrySchematyc/Env/Elements/EnvSignal.h>
#include <CryCore/StaticInstanceList.h>
#include <CryNetwork/Rmi.h>

namespace
{
	static void RegisterPlayerComponent(Schematyc::IEnvRegistrar& registrar)
	{
		Schematyc::CEnvRegistrationScope scope = registrar.Scope(IEntity::GetEntityScopeGUID());
		{
			Schematyc::CEnvRegistrationScope componentScope = scope.Register(SCHEMATYC_MAKE_ENV_COMPONENT(CPlayerComponent));

			{
				auto pFunction = SCHEMATYC_MAKE_ENV_FUNCTION(&CPlayerComponent::Jump, "{DF2A9AE7-7724-4684-89F6-9DF336F61AC2}"_cry_guid, "Jump");
				componentScope.Register(pFunction);
			}

			{
				auto pFunction = SCHEMATYC_MAKE_ENV_FUNCTION(&CPlayerComponent::Shoot, "{899ADE13-94B7-417C-8F41-1B4D69F93904}"_cry_guid, "Shoot");
				componentScope.Register(pFunction);
			}

			componentScope.Register(SCHEMATYC_MAKE_ENV_SIGNAL(CPlayerComponent::SInitializeLocalPlayer));
		}
	}

	CRY_STATIC_AUTO_REGISTER_FUNCTION(&RegisterPlayerComponent);
}

static void ReflectType(Schematyc::CTypeDesc<CPlayerComponent::SInitializeLocalPlayer>& desc)
{
	desc.SetGUID("{A0411357-E8B6-4BDC-AF4F-DF49263897DF}"_cry_guid);
	desc.SetLabel("Initialize Local Player");
}

void CPlayerComponent::Initialize()
{
	// The character controller is responsible for maintaining player physics
	m_pCharacterController = m_pEntity->GetOrCreateComponent<Cry::DefaultComponents::CCharacterControllerComponent>();

	// Create the advanced animation component, responsible for updating Mannequin and animating the player
	m_pAnimationComponent = m_pEntity->GetOrCreateComponent<Cry::DefaultComponents::CAdvancedAnimationComponent>();

	// Load the character and Mannequin data from file
	m_pAnimationComponent->LoadFromDisk();

	// Acquire fragment and tag identifiers to avoid doing so each update
	m_idleFragmentId = m_pAnimationComponent->GetFragmentId("Idle");
	m_walkFragmentId = m_pAnimationComponent->GetFragmentId("Walk");

	// Get and initialize the navigation component
	m_pNavigationComponent = m_pEntity->GetOrCreateComponent<IEntityNavigationComponent>();

	m_pNavigationComponent->SetStateUpdatedCallback([this](const Vec3& recommendedVelocity)
	{
		m_pCharacterController->ChangeVelocity(recommendedVelocity, Cry::DefaultComponents::CCharacterControllerComponent::EChangeVelocityMode::SetAsTarget);
	});
	
	// Register the RemoteReviveOnClient function as a Remote Method Invocation (RMI) that can be executed by the server on clients
	SRmi<RMI_WRAP(&CPlayerComponent::RemoteReviveOnClient)>::Register(this, eRAT_NoAttach, false, eNRT_ReliableOrdered);
	SRmi<RMI_WRAP(&CPlayerComponent::RemoteShootOnServer)>::Register(this, eRAT_NoAttach, false, eNRT_ReliableOrdered);
}

void CPlayerComponent::InitializeLocalPlayer()
{
	// Set the animation component to always update when out of view
	if (ICharacterInstance* pCharacter = m_pAnimationComponent->GetCharacter())
	{
		pCharacter->SetFlags(pCharacter->GetFlags() | CS_FLAG_UPDATE_ALWAYS);

		if (ISkeletonPose* pPose = pCharacter->GetISkeletonPose())
		{
			pPose->SetForceSkeletonUpdate(2);
		}
	}
	
	// Create the camera component, will automatically update the viewport every frame
	m_pCameraComponent = m_pEntity->GetOrCreateComponent<Cry::DefaultComponents::CCameraComponent>();

	m_pCameraComponent->Activate();

	// Create the audio listener component.
	m_pAudioListenerComponent = m_pEntity->GetOrCreateComponent<Cry::Audio::DefaultComponents::CListenerComponent>();
	
	// Get the input component, wraps access to action mapping so we can easily get callbacks when inputs are triggered
	m_pInputComponent = m_pEntity->GetOrCreateComponent<Cry::DefaultComponents::CInputComponent>();
	
	// Register the walk action
	m_pInputComponent->RegisterAction("player", "walkto", [this](int activationMode, float value)
	{
		if (m_pCursorEntity != nullptr && activationMode == eAAM_OnPress)
		{
			m_pNavigationComponent->NavigateTo(m_pCursorEntity->GetWorldPos());
		}
	});

	// Bind the walkto action to left mouse click
	m_pInputComponent->BindAction("player", "walkto", eAID_KeyboardMouse, EKeyId::eKI_Mouse1);
	
	// Spawn the cursor
	if (!gEnv->IsEditor())
	{
		SpawnCursorEntity();
	}

	// Our local player has initialized, now call the Schematyc signal for it
	if (Schematyc::IObject* const pSchematycObject = m_pEntity->GetSchematycObject())
	{
		m_pEntity->GetSchematycObject()->ProcessSignal(SInitializeLocalPlayer(), GetGUID());
	}
}

Cry::Entity::EventFlags CPlayerComponent::GetEventMask() const
{
	return
		Cry::Entity::EEvent::BecomeLocalPlayer |
		Cry::Entity::EEvent::Update |
		Cry::Entity::EEvent::Reset;
}

void CPlayerComponent::ProcessEvent(const SEntityEvent& event)
{
	switch (event.event)
	{
	case Cry::Entity::EEvent::BecomeLocalPlayer:
	{
		InitializeLocalPlayer();
	}
	break;
	case Cry::Entity::EEvent::Update:
	{
		// Don't update the player if we haven't spawned yet
		if(!m_isAlive)
			return;
		
		const float frameTime = event.fParam[0];

		// Update the in-world cursor position
		UpdateCursor(frameTime);

		// Update the animation state of the character
		UpdateAnimation(frameTime);

		if (IsLocalClient())
		{
			// Update the camera component offset
			UpdateCamera(frameTime);
		}
	}
	break;
	case Cry::Entity::EEvent::Reset:
	{
		// Disable player when leaving game mode.
		m_isAlive = event.nParam[0] != 0;

		if (event.nParam[0] != 0)
		{
			// Reset player when entering game mode
			OnReadyForGameplayOnServer();
		}

		// Check if we're entering game mode
		if (m_isAlive)
		{
			SpawnCursorEntity();
		}
		else
		{
			// Removed by Sandbox
			m_pCursorEntity = nullptr;
		}
	}
	break;
	}
}

void CPlayerComponent::SpawnCursorEntity()
{
	if (m_pCursorEntity)
	{
		gEnv->pEntitySystem->RemoveEntity(m_pCursorEntity->GetId());
	}

	SEntitySpawnParams spawnParams;
	// No need for a special class!
	spawnParams.pClass = gEnv->pEntitySystem->GetClassRegistry()->GetDefaultClass();

	// Spawn the cursor
	m_pCursorEntity = gEnv->pEntitySystem->SpawnEntity(spawnParams);

	// Load geometry
	const int geometrySlot = 0;
	m_pCursorEntity->LoadGeometry(geometrySlot, "%ENGINE%/EngineAssets/Objects/primitive_sphere.cgf");

	// Scale the cursor down a bit
	m_pCursorEntity->SetScale(Vec3(0.1f));
	m_pCursorEntity->SetViewDistRatio(255);

	// Load the custom cursor material
	IMaterial* pCursorMaterial = gEnv->p3DEngine->GetMaterialManager()->LoadMaterial("Materials/cursor");
	m_pCursorEntity->SetMaterial(pCursorMaterial);
}

void CPlayerComponent::UpdateAnimation(float frameTime)
{
	// Update active fragment
	const FragmentID& desiredFragmentId = m_pCharacterController->IsWalking() ? m_walkFragmentId : m_idleFragmentId;
	if (m_activeFragmentId != desiredFragmentId)
	{
		m_activeFragmentId = desiredFragmentId;
		m_pAnimationComponent->QueueFragmentWithId(m_activeFragmentId);
	}

	if (m_pCharacterController->IsWalking())
	{
		Quat newRotation = Quat::CreateRotationVDir(m_pCharacterController->GetMoveDirection());

		Ang3 ypr = CCamera::CreateAnglesYPR(Matrix33(newRotation));

		// We only want to affect Z-axis rotation, zero pitch and roll
		ypr.y = 0;
		ypr.z = 0;

		// Re-calculate the quaternion based on the corrected yaw
		newRotation = Quat(CCamera::CreateOrientationYPR(ypr));

		// Send updated transform to the entity, only orientation changes
		m_pEntity->SetPosRotScale(m_pEntity->GetWorldPos(), newRotation, Vec3(1, 1, 1));
	}
	else
	{
		Ang3 ypr = CCamera::CreateAnglesYPR(Matrix33(m_pEntity->GetWorldAngles()));

		// We only want to affect Z-axis rotation, zero pitch and roll
		ypr.y = 0;
		ypr.z = 0;

		// Re-calculate the quaternion based on the corrected yaw
		Quat newRotation = Quat(CCamera::CreateOrientationYPR(ypr));

		// Send updated transform to the entity, only orientation changes
		m_pEntity->SetPosRotScale(m_pEntity->GetWorldPos(), newRotation, Vec3(1, 1, 1));
	}
}

void CPlayerComponent::UpdateCamera(float frameTime)
{
	// Start with rotating the camera isometric style
	Matrix34 localTransform = IDENTITY;
	localTransform.SetRotation33(Matrix33(m_pEntity->GetWorldRotation().GetInverted()) * Matrix33::CreateRotationXYZ(Ang3(DEG2RAD(-45), 0, DEG2RAD(-45))));

	// Offset the player along the forward axis (normally back)
	// Also offset upwards
	localTransform.SetTranslation(-localTransform.GetColumn1() * m_viewDistanceFromPlayer);

	if (m_pCameraComponent)
	{
		m_pCameraComponent->SetTransformMatrix(localTransform);
	}
	if (m_pAudioListenerComponent)
	{
		m_pAudioListenerComponent->SetOffset(localTransform.GetTranslation());
	}

	if (!m_pCameraComponent || !m_pAudioListenerComponent)
	{
		gEnv->pRenderer->GetIRenderAuxGeom()->Draw2dLabel(50.0f, 50.0f, 1.5f, Col_Orange, false, "Player Schematyc was edited, please reopen the level if it isn't playing propely");
	}
}

void CPlayerComponent::UpdateCursor(float frameTime)
{
	float mouseX, mouseY;
	gEnv->pHardwareMouse->GetHardwareMouseClientPosition(&mouseX, &mouseY);

	// Invert mouse Y
	mouseY = gEnv->pRenderer->GetHeight() - mouseY;

	Vec3 vPos0(0, 0, 0);
	gEnv->pRenderer->UnProjectFromScreen(mouseX, mouseY, 0, &vPos0.x, &vPos0.y, &vPos0.z);

	Vec3 vPos1(0, 0, 0);
	gEnv->pRenderer->UnProjectFromScreen(mouseX, mouseY, 1, &vPos1.x, &vPos1.y, &vPos1.z);

	Vec3 vDir = vPos1 - vPos0;
	vDir.Normalize();

	const unsigned int rayFlags = rwi_stop_at_pierceable | rwi_colltype_any;
	ray_hit hit;

	if (gEnv->pPhysicalWorld->RayWorldIntersection(vPos0, vDir * gEnv->p3DEngine->GetMaxViewDistance(), ent_all, rayFlags, &hit, 1))
	{
		if (m_pCursorEntity != nullptr)
		{
			m_pCursorEntity->SetPosRotScale(hit.pt, IDENTITY, m_pCursorEntity->GetScale());
		}
	}
}

void CPlayerComponent::Jump()
{
	m_pCharacterController->AddVelocity(Vec3(0, 0, -m_pCharacterController->GetVelocity().z + m_jumpHeight));
}

void CPlayerComponent::Shoot()
{
	if (ICharacterInstance *pCharacter = m_pAnimationComponent->GetCharacter())
	{
		IAttachment* pBarrelOutAttachment = pCharacter->GetIAttachmentManager()->GetInterfaceByName("barrel_out");

		if (pBarrelOutAttachment != nullptr)
		{
			QuatTS bulletOrigin = pBarrelOutAttachment->GetAttWorldAbsolute();

			RemoteShootParams params;
			params.position = bulletOrigin.t;
			params.rotation = bulletOrigin.q;

			// Tell server to spawn the bullet
			SRmi<RMI_WRAP(&CPlayerComponent::RemoteShootOnServer)>::InvokeOnServer(this, std::move(params));
		}
	}
}

bool CPlayerComponent::IsSwimming()
{
	if (m_pCharacterController)
	{
		if (IEntity* pEntity = m_pCharacterController->GetEntity())
		{
			if (IPhysicalEntity* pPhysEnt = pEntity->GetPhysicalEntity())
			{
				pe_player_dynamics dyn;
				pPhysEnt->GetParams(&dyn);

				return dyn.bSwimming;
			}
		}
	}

	return false;
}

void CPlayerComponent::OnReadyForGameplayOnServer()
{
	CRY_ASSERT(gEnv->bServer, "This function should only be called on the server!");
	
	const Matrix34 newTransform = CSpawnPointComponent::GetFirstSpawnPointTransform();
	
	Revive(newTransform);
	
	// Invoke the RemoteReviveOnClient function on all remote clients, to ensure that Revive is called across the network
	SRmi<RMI_WRAP(&CPlayerComponent::RemoteReviveOnClient)>::InvokeOnOtherClients(this, RemoteReviveParams{ newTransform.GetTranslation(), Quat(newTransform) });
	
	// Go through all other players, and send the RemoteReviveOnClient on their instances to the new player that is ready for gameplay
	const int channelId = m_pEntity->GetNetEntity()->GetChannelId();
	CGamePlugin::GetInstance()->IterateOverPlayers([this, channelId](CPlayerComponent& player)
	{
		// Don't send the event for the player itself (handled in the RemoteReviveOnClient event above sent to all clients)
		if (player.GetEntityId() == GetEntityId())
			return;

		// Only send the Revive event to players that have already respawned on the server
		if (!player.m_isAlive)
			return;

		// Revive this player on the new player's machine, on the location the existing player was currently at
		const QuatT currentOrientation = QuatT(player.GetEntity()->GetWorldTM());
		SRmi<RMI_WRAP(&CPlayerComponent::RemoteReviveOnClient)>::InvokeOnClient(&player, RemoteReviveParams{ currentOrientation.t, currentOrientation.q }, channelId);
	});
}

bool CPlayerComponent::RemoteShootOnServer(RemoteShootParams&& params, INetChannel* pNetChannel)
{
	SEntitySpawnParams spawnParams;
	spawnParams.pClass = gEnv->pEntitySystem->GetClassRegistry()->FindClass("schematyc::schematycs::bullet");

	spawnParams.vPosition = params.position;
	spawnParams.qRotation = params.rotation;

	const float bulletScale = 0.05f;
	spawnParams.vScale = Vec3(bulletScale);

	// Spawn the entity
	if (IEntity* pEntity = gEnv->pEntitySystem->SpawnEntity(spawnParams))
	{
		// See Bullet.cpp, bullet is propelled in  the rotation and position the entity was spawned with
		//pEntity->CreateComponentClass<CBulletComponent>();
	}

	return true;
}

bool CPlayerComponent::RemoteReviveOnClient(RemoteReviveParams&& params, INetChannel* pNetChannel)
{
	// Call the Revive function on this client
	Revive(Matrix34::Create(Vec3(1.f), params.rotation, params.position));

	return true;
}

void CPlayerComponent::Revive(const Matrix34& transform)
{
	m_isAlive = true;
	
	// Set the entity transformation, except if we are in the editor
	// In the editor case we always prefer to spawn where the viewport is
	if(!gEnv->IsEditor())
	{
		m_pEntity->SetWorldTM(transform);
	}
	
	// Apply the character to the entity and queue animations
	m_pAnimationComponent->ResetCharacter();
	m_pCharacterController->Physicalize();

	m_activeFragmentId = FRAGMENT_ID_INVALID;
}