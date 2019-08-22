#include "TPS_studyCharacter.h"
#include "Blueprint/UserWidget.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DataTable.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "TimerManager.h"
#include "TPSAnimInterface.h"
#include "TPS_FunctionLibrary.h"
#include "TPS_Projectile.h"
#include "DrawDebugHelpers.h"
#include "TPS_Weapon.h"

// 0.a CONSTRUCTION
ATPS_studyCharacter::ATPS_studyCharacter() {
	// basic component setup:
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	RangedWeapon = CreateDefaultSubobject<UTPS_Weapon>(TEXT("RangedWeapon"));
	// basic component setup:
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 300.0f;
	FollowCamera->bUsePawnControlRotation = false;
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	// inherited component setup:
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	bUseControllerRotationYaw = false;
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
	GetCharacterMovement()->AirControl = 0.2f;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->JumpZVelocity = 600.f;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f);
	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -97.0f));
	GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	PrimaryActorTick.bStartWithTickEnabled = false;
	// aiming setup:
	if (AimStats.Num() == 0) { AimStats.SetNum(1); };
	AimStats[0].CamBoom.SocketOffset = GetCameraBoom()->SocketOffset;
	AimStats[0].CamBoom.TargetArmLength = GetCameraBoom()->TargetArmLength;
	AimStats[0].CharMov.MaxAcceleration = GetCharacterMovement()->MaxAcceleration;
	AimStats[0].CharMov.MaxWalkSpeed = GetCharacterMovement()->MaxWalkSpeed;
	AimStats[0].FollCam.FieldOfView = GetFollowCamera()->FieldOfView;
	// fire setup:
	SwitchWeaponMesh();
}

float ATPS_studyCharacter::GetHP() { return CharacterStat.HP; }
void ATPS_studyCharacter::SetHP(float val) { CharacterStat.HP = val; }
float ATPS_studyCharacter::GetMP() { return CharacterStat.MP; }
void ATPS_studyCharacter::SetMP(float val) { CharacterStat.MP = val; }

void ATPS_studyCharacter::BeginPlay() {
	Super::BeginPlay();
	// aiming setup:
	if (AimingTable != nullptr) 
	{ 
		AimingNames = AimingTable->GetRowNames(); 
	}

	int aimingNamesCount = AimingNames.Num();
	FName currentAimingName;
	static const FString contextString(TEXT("Aiming name"));
	struct FAimingStatCompact* aimStatRow;
	AimStats.SetNum(1 + aimingNamesCount);

	for (int i = 0; i < AimingNames.Num(); i++) 
	{
		currentAimingName = AimingNames[i];
		aimStatRow = AimingTable->FindRow<FAimingStatCompact>(currentAimingName, contextString, true);
		AimStats[1 + i].CamBoom = aimStatRow->AimStat.CamBoom;
		AimStats[1 + i].CharMov = aimStatRow->AimStat.CharMov;
		AimStats[1 + i].FollCam = aimStatRow->AimStat.FollCam;
	}

	// aiming timeline setup:
	FOnTimelineFloat onAimingTimeCallback;
	FOnTimelineEventStatic onAimingTimeFinishedCallback;
	if (FloatCurve) 
	{
		AimingTimeline = NewObject<UTimelineComponent>(this, FName("AimingTimeline"));
		AimingTimeline->CreationMethod = EComponentCreationMethod::UserConstructionScript;
		this->BlueprintCreatedComponents.Add(AimingTimeline);
		AimingTimeline->SetNetAddressable();
		AimingTimeline->SetPropertySetObject(this);
		AimingTimeline->SetDirectionPropertyName(FName("TimeAimingDirection"));
		AimingTimeline->SetLooping(false);
		AimingTimeline->SetTimelineLength(1.0f);
		AimingTimeline->SetTimelineLengthMode(ETimelineLengthMode::TL_LastKeyFrame);
		AimingTimeline->SetPlaybackPosition(0.0f, false);
		onAimingTimeCallback.BindUFunction(this, FName(TEXT("TimeAiming")));
		onAimingTimeFinishedCallback.BindUFunction(this, FName(TEXT("TimeFinishAiming")));
		AimingTimeline->AddInterpFloat(FloatCurve, onAimingTimeCallback);
		AimingTimeline->SetTimelineFinishedFunc(onAimingTimeFinishedCallback);
		AimingTimeline->RegisterComponent();
	}
	// weapon setup:
	if (WeaponModeTable != nullptr) { WeaponNames = WeaponModeTable->GetRowNames(); }
	SetWeaponMode(0);
}

void ATPS_studyCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) 
{
	check(PlayerInputComponent);
	//AttackAction
	PlayerInputComponent->BindAction("AttackAction", IE_Pressed, this, &ATPS_studyCharacter::FirePress);
	PlayerInputComponent->BindAction("AttackAction", IE_Released, this, &ATPS_studyCharacter::FireRelease);
	PlayerInputComponent->BindAction("AimAction", IE_Pressed, this, &ATPS_studyCharacter::Aiming);
	PlayerInputComponent->BindAction("AimAction", IE_Released, this, &ATPS_studyCharacter::AimingStop);
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);
	PlayerInputComponent->BindAction("Weapon1", IE_Pressed, this, &ATPS_studyCharacter::SetWeaponIndexWithNumpad_1);
	PlayerInputComponent->BindAction("Weapon2", IE_Pressed, this, &ATPS_studyCharacter::SetWeaponIndexWithNumpad_2);
	PlayerInputComponent->BindAction("Weapon3", IE_Pressed, this, &ATPS_studyCharacter::SetWeaponIndexWithNumpad_3);
	PlayerInputComponent->BindAction("Weapon4", IE_Pressed, this, &ATPS_studyCharacter::SetWeaponIndexWithNumpad_4);
	PlayerInputComponent->BindAction("ChangeWeaponUp", IE_Pressed, this, &ATPS_studyCharacter::SetWeaponIndexWithMouseWheel_Up);
	PlayerInputComponent->BindAction("ChangeWeaponDown", IE_Pressed, this, &ATPS_studyCharacter::SetWeaponIndexWithMouseWheel_Down);
	PlayerInputComponent->BindAxis("MoveForward", this, &ATPS_studyCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &ATPS_studyCharacter::MoveRight);
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &ATPS_studyCharacter::LookRightAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &ATPS_studyCharacter::LookUpAtRate);
}

void ATPS_studyCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (AimingTimeline) 
	{
		AimingTimeline->TickComponent(DeltaSeconds, ELevelTick::LEVELTICK_TimeOnly, NULL);
	}
}
// 0.z CONSTRUCTION

// 1.a NAVIGATION
float ATPS_studyCharacter::GetNormalizedForward() { return NormalizedForward; }
float ATPS_studyCharacter::GetNormalizedRight() { return NormalizedRight; }

ETriggerMechanism ATPS_studyCharacter::GetTriggerMechanism() const { return CurrentWeapon.Trigger; }

FName ATPS_studyCharacter::GetWeaponName()
{
	return WeaponNames[WeaponIndex];
}

void ATPS_studyCharacter::MoveForward(float Value)
{
	if ((Controller != NULL) && (Value != 0.0f))
	{
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		AddMovementInput(Direction, Value);
		if (bIsAiming)
		{
			bForwardInputPressed = true;
			NormalizedForward = AssignNormalizedVelo(Value, bRightInputPressed);
		}
	} 
	else 
	{
		bForwardInputPressed = false;
		NormalizedForward = 0.0f;
	}
}

void ATPS_studyCharacter::MoveRight(float Value)
{
	if ((Controller != NULL) && (Value != 0.0f)) 
	{
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		AddMovementInput(Direction, Value);
		if (bIsAiming) 
		{
			bRightInputPressed = true;
			NormalizedRight = AssignNormalizedVelo(Value, bForwardInputPressed);
		}
	} 
	else 
	{
		bRightInputPressed = false;
		NormalizedRight = 0.0f;
	}
}

void ATPS_studyCharacter::LookRightAtRate(float Rate)
{
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void ATPS_studyCharacter::LookUpAtRate(float Rate)
{
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}
// 1.z NAVIGATION

// 2.a AIMING
bool ATPS_studyCharacter::GetIsAiming() { return bIsAiming; }

void ATPS_studyCharacter::Aiming() {
	if (!IsAbleToAim()) { return; }
	Aiming_Setup(true);
	bIsTransitioningAiming = true;
	AimingTimeline->Play();
}

void ATPS_studyCharacter::AimingStop()
{
	Aiming_Setup(false);
	AimingTimeline->Reverse();
}

void ATPS_studyCharacter::Aiming_Setup(const bool isAiming)
{
	bIsAiming = isAiming;
	OrientCharacter(isAiming);
	SetIsTransitioningAiming(isAiming);
	float speed = (isAiming) ? AimingSpeed : StopAimingSpeed;
	if (speed <= 0.0f) { speed = 1.0f; };
	AimingTimeline->SetPlayRate(1.0f / speed);
}

void ATPS_studyCharacter::OrientCharacter(bool bMyCharIsAiming)
{
	FollowCamera->bUsePawnControlRotation = bMyCharIsAiming;
	bUseControllerRotationYaw = bMyCharIsAiming;
	GetCharacterMovement()->bOrientRotationToMovement = !bMyCharIsAiming;
}

void ATPS_studyCharacter::TimeAiming(float val)
{
	int A = AimStatStartIndex;
	int B = AimStatTargetIndex;
	float aimingFieldOfView = AimStats[B].FollCam.FieldOfView;
	float aimingMaxAcceleration = AimStats[B].CharMov.MaxAcceleration;
	float aimingTargetArmLength = AimStats[B].CamBoom.TargetArmLength;
	float aimingWalkSpeed = AimStats[B].CharMov.MaxWalkSpeed;
	float defaultFieldOfView = AimStats[A].FollCam.FieldOfView;
	float defaultMaxAcceleration = AimStats[A].CharMov.MaxAcceleration;
	float defaultTargetArmLength = AimStats[A].CamBoom.TargetArmLength;
	float defaultWalkSpeed = AimStats[A].CharMov.MaxWalkSpeed;
	FVector aimingSocketOffset = AimStats[B].CamBoom.SocketOffset;
	FVector defaultSocketOffset = AimStats[A].CamBoom.SocketOffset;
	GetCameraBoom()->TargetArmLength = FMath::Lerp(defaultTargetArmLength, aimingTargetArmLength, val);
	GetCameraBoom()->SocketOffset = FMath::Lerp(defaultSocketOffset, aimingSocketOffset, val);
	GetCharacterMovement()->MaxWalkSpeed = FMath::Lerp(defaultWalkSpeed, aimingWalkSpeed, val);
	GetCharacterMovement()->MaxAcceleration = FMath::Lerp(defaultMaxAcceleration, aimingMaxAcceleration, val);
	GetFollowCamera()->SetFieldOfView(FMath::Lerp(defaultFieldOfView, aimingFieldOfView, val));
}

void ATPS_studyCharacter::TimeFinishAiming() 
{ 
	bIsTransitioningAiming = false; 
	OnAimingSucceed();
}
// 2.z AIMING

// 3.a FIRE
bool ATPS_studyCharacter::IsAbleToRepeatAutoFire_Implementation() { return bIsTriggerPressed; }

bool ATPS_studyCharacter::IsAbleToFire_Implementation() { return !GetCharacterMovement()->IsFalling(); }

bool ATPS_studyCharacter::IsEnoughForWeaponCost() 
{
	if (CurrentWeapon.WeaponCost == EWeaponCost::Nothing) { return true; }
	switch (CurrentWeapon.WeaponCost) {
	case EWeaponCost::Ammo:
		return IsAmmoEnough(CurrentWeapon.AmmoType);
	case EWeaponCost::Energy:
		return IsEnergyEnough(CurrentWeapon.EnergyType);
	default:
		return false;
	}
}

bool ATPS_studyCharacter::IsThereStillAmmoLeft() 
{
	switch (CurrentWeapon.AmmoType) 
	{
	case EAmmoType::StandardAmmo:
		return Ammunition.StandardAmmo <= 0;
	case EAmmoType::RifleAmmo:
		return Ammunition.RifleAmmo <= 0;
	case EAmmoType::ShotgunAmmo:
		return Ammunition.ShotgunAmmo <= 0;
	case EAmmoType::Rocket:
		return Ammunition.Rocket <= 0;
	case EAmmoType::Arrow:
		return Ammunition.Arrow <= 0;
	case EAmmoType::Grenade:
		return Ammunition.Grenade <= 0;
	case EAmmoType::Mine:
		return Ammunition.Mine <= 0;
	default:
		return false;
	}
}

bool ATPS_studyCharacter::GetIsTriggerPressed() { return bIsTriggerPressed; }

FRotator ATPS_studyCharacter::GetNewMuzzleRotationFromLineTrace(FTransform SocketTransform) 
{
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetOwner());
	QueryParams.AddIgnoredActor(this);
	FHitResult HitTrace;
	FVector StartTrace = GetFollowCamera()->GetComponentLocation();
	FRotator CameraRotation = GetFollowCamera()->GetComponentRotation();
	FVector LookDirection = UKismetMathLibrary::GetForwardVector(CameraRotation);
	FVector EndTrace = StartTrace + LookDirection * 100.0f * 3000.0f;
	FVector TargetLocation;
	FRotator MuzzleLookRotation;
	if (GetWorld()->LineTraceSingleByChannel(HitTrace, StartTrace, EndTrace, ECC_Visibility, QueryParams)) 
	{
		TargetLocation = HitTrace.Location;
		MuzzleLookRotation = UKismetMathLibrary::FindLookAtRotation(SocketTransform.GetLocation(), TargetLocation);
	}
	//DrawDebugLine(GetWorld(), StartTrace, EndTrace, FColor::White, false, 3.0f, 0, 1.0f);
	DrawDebugLine(GetWorld(), SocketTransform.GetLocation(), TargetLocation, FColor::Blue, false, 3.0f, 0, 1.0f);
	return MuzzleLookRotation;
}
void ATPS_studyCharacter::FirePress() 
{
	bIsTriggerPressed = true;
	if (!(bIsAiming && bIsFireRatePassed && IsEnoughForWeaponCost())) { return; }

	switch (CurrentWeapon.Trigger) 
	{
	case ETriggerMechanism::PressTrigger:
		FireStandardTrigger();
		OnFire();
		break;
	case ETriggerMechanism::AutomaticTrigger:
		UE_LOG(LogTemp, Log, TEXT("automatic trigger checked"));
		FireAutomaticTrigger();
		break;
	case ETriggerMechanism::ReleaseTrigger:
		UE_LOG(LogTemp, Log, TEXT("release trigger checked"));
		Fire_Hold();
		break;
	case ETriggerMechanism::OnePressAutoTrigger:
		UE_LOG(LogTemp, Log, TEXT("one press trigger checked"));
		FireAutomaticTriggerOnePress();
		break;
	default:
		FireStandardTrigger();
	}
}

void ATPS_studyCharacter::FireRelease()
{
	bIsTriggerPressed = false;
	if (CurrentWeapon.Trigger == ETriggerMechanism::ReleaseTrigger) 
	{
		Fire_Release();
	}
}

void ATPS_studyCharacter::FireAutomaticTrigger()
{
	if (!(bIsTriggerPressed && bIsAiming && bIsFireRatePassed && IsEnoughForWeaponCost())) { return; }
	FireStandardTrigger();
	OnFire();
}

void ATPS_studyCharacter::FireAutomaticTriggerOnePress() 
{
}

void ATPS_studyCharacter::Fire_Hold() 
{
}

void ATPS_studyCharacter::Fire_Release() 
{
}

void ATPS_studyCharacter::FireStandardTrigger()
{
	TimerFireRateStart();
	PlayFireMontage();

	switch (CurrentWeapon.WeaponCost)
	{
	case EWeaponCost::Nothing:
		FireUnlimited();
		break;
	case EWeaponCost::Ammo:
		FireAmmo();
		break;
	case EWeaponCost::Energy:
		FireEnergy();
		break;
	default:
		break;
	}
}

void ATPS_studyCharacter::FireAmmo() 
{
	switch (CurrentWeapon.AmmoType) 
	{
	case EAmmoType::StandardAmmo:
		FireAmmoProjectile(&Ammunition.StandardAmmo);
		break;
	case EAmmoType::RifleAmmo:
		FireAmmoProjectile(&Ammunition.RifleAmmo);
		break;
	case EAmmoType::ShotgunAmmo:
		FireAmmoProjectile(&Ammunition.ShotgunAmmo);
		break;
	case EAmmoType::Rocket:
		FireAmmoProjectile(&Ammunition.Rocket);
		break;
	case EAmmoType::Arrow:
		FireAmmoProjectile(&Ammunition.Arrow);
		break;
	case EAmmoType::Grenade:
		FireAmmoProjectile(&Ammunition.Grenade);
		break;
	case EAmmoType::Mine:
		FireAmmoProjectile(&Ammunition.Mine);
		break;
	default:
		break;
	}
}

void ATPS_studyCharacter::FireEnergy()
{
	switch (CurrentWeapon.EnergyType) 
	{
	case EEnergyType::MP:
		FireEnergyProjectile( &CharacterStat.MP );
		break;
	case EEnergyType::Battery:
		FireEnergyProjectile( &EnergyExternal.Battery );
		break;
	case EEnergyType::Fuel:
		FireEnergyProjectile( &EnergyExternal.Fuel );
		break;
	case EEnergyType::Overheat:
		FireEnergyProjectile( &EnergyExternal.Overheat );
	}
}

void ATPS_studyCharacter::FireUnlimited() 
{
	TArray<FName> MuzzleName = CurrentWeapon.SocketName;
	int MuzzleCount = MuzzleName.Num();

	for (int i = 0; i < MuzzleCount; i++) 
	{
		SpawnProjectile(WeaponInWorld, MuzzleName, GetWorld(), i);
	}
}

void ATPS_studyCharacter::FireAmmoProjectile(int* Ammo)
{
	TArray<FName> MuzzleName = CurrentWeapon.SocketName;
	int MuzzleCount = MuzzleName.Num();
	int CurrentAmmo = *Ammo;

	for (int i = 0; i < MuzzleCount; i++)
	{
		if (CurrentAmmo <= 0) {
			OnRunOutOfAmmoDuringMultipleFire();
			break;
		}

		CurrentAmmo--;
		SpawnProjectile(WeaponInWorld, MuzzleName, GetWorld(), i);
	}

	*Ammo = CurrentAmmo;
}

void ATPS_studyCharacter::FireEnergyProjectile(float* MyEnergy) 
{
	TArray<FName> MuzzleName = CurrentWeapon.SocketName;
	int MuzzleCount = MuzzleName.Num();
	float CurrentEnergy = *MyEnergy;
	float EnergyCostPerShot = CurrentWeapon.EnergyUsePerShot;

	if (CurrentWeapon.EnergyType != EEnergyType::Overheat)
	{
		for (int i = 0; i < MuzzleCount; i++) 
		{
			if (CurrentEnergy < EnergyCostPerShot)
			{
				OnRunOutOfAmmoDuringMultipleFire();
				break;
			}

			CurrentEnergy -= EnergyCostPerShot;
			SpawnProjectile(WeaponInWorld, MuzzleName, GetWorld(), i);
		}
	}
	else 
	{
		for (int i = 0; i < MuzzleCount; i++)
		{
			if (CurrentEnergy >= 100.0f)
			{
				OnRunOutOfAmmoDuringMultipleFire();
				break;
			}

			CurrentEnergy += EnergyCostPerShot;
			SpawnProjectile(WeaponInWorld, MuzzleName, GetWorld(), i);
		}
	}
	*MyEnergy = CurrentEnergy;
}

void ATPS_studyCharacter::SpawnProjectile(USceneComponent* MyWeaponInWorld, TArray<FName> MuzzleName, UWorld* MyWorld, int i)
{
	FTransform MuzzleTransform = MyWeaponInWorld->GetSocketTransform(MuzzleName[i]);
	FTransform SpawnTransform = FTransform(GetNewMuzzleRotationFromLineTrace(MuzzleTransform), MuzzleTransform.GetLocation(), MuzzleTransform.GetScale3D());
	UClass* ProjectileClass = UGameplayStatics::GetObjectClass(TheProjectile);
	ATPS_Projectile* MyProjectile = MyWorld->SpawnActorDeferred<ATPS_Projectile>(ATPS_Projectile::StaticClass(), SpawnTransform);

	MyProjectile->SetUpProjectile(CurrentProjectile);
	MyProjectile->FinishSpawning(SpawnTransform);
}

void ATPS_studyCharacter::PlayFireMontage() {
	UAnimMontage* fireMontage;
	if (ShooterState.CharacterWeaponMontage.Num() > 0) {
		fireMontage = ShooterState.CharacterWeaponMontage[0];
		if (fireMontage) {
			float playRate = UTPS_FunctionLibrary::GetNewPlayRateForMontage(CurrentWeapon.FireRateAndOther[0], fireMontage);
			PlayAnimMontage(fireMontage, playRate);
		}
	}
}
void ATPS_studyCharacter::TimerFireRateStart() {
	bIsFireRatePassed = false;
	GetWorldTimerManager().ClearTimer(FireRateTimer);
	GetWorldTimerManager().SetTimer(FireRateTimer, this, &ATPS_studyCharacter::TimerFireRateReset, CurrentWeapon.FireRateAndOther[0]);
}
void ATPS_studyCharacter::TimerFireRateReset() {
	bIsFireRatePassed = true;
	GetWorldTimerManager().ClearTimer(FireRateTimer);
	if (CurrentWeapon.Trigger == ETriggerMechanism::AutomaticTrigger) {
		FireAutomaticTrigger();
	}
}
// 3.z FIRE

// 4.a SWITCH WEAPON
bool ATPS_studyCharacter::IsAbleToSwitchWeapon_Implementation() 
{
	bool bIsOnTheGround = !GetCharacterMovement()->IsFalling();
	bool bIsNotAiming = !GetIsAiming();
	return bIsOnTheGround && bIsNotAiming;
}

bool ATPS_studyCharacter::IsAbleToAim_Implementation() 
{
	return !GetCharacterMovement()->IsFalling();
}

int ATPS_studyCharacter::GetLastWeaponIndex() const { return LastWeaponIndex; }

int ATPS_studyCharacter::GetWeaponIndex() const { return WeaponIndex; }

void ATPS_studyCharacter::SetWeaponMode(int MyWeaponIndex) 
{
	FName CurrentWeaponName = WeaponNames[MyWeaponIndex];
	static const FString ContextString(TEXT("Weapon Mode"));
	struct FWeaponModeCompact* WeaponModeRow;
	WeaponModeRow = WeaponModeTable->FindRow<FWeaponModeCompact>(CurrentWeaponName, ContextString, true);

	if (WeaponModeRow) 
	{
		FWeaponMode CurrentWeaponMode = WeaponModeRow->WeaponMode;
		ShooterState = CurrentWeaponMode.Shooter;
		CurrentWeapon = CurrentWeaponMode.Weapon;
		CurrentProjectile = CurrentWeaponMode.Projectile;
		UE_LOG(LogTemp, Log, TEXT("weapon changed to %i and the speed is %f"), MyWeaponIndex, CurrentProjectile.Muzzle.InitialSpeedAndOther[0]);
	}
}
void ATPS_studyCharacter::SetWeaponIndexWithMouseWheel(bool isUp) 
{
	LastWeaponIndex = WeaponIndex;

	if (IsAbleToSwitchWeapon()) 
	{
		int counter = isUp ? 1 : -1;
		int withinRange = (WeaponIndex + counter) % WeaponNames.Num();

		WeaponIndex = (withinRange >= 0) ? withinRange : WeaponNames.Num() - 1;
		SetWeaponMode(WeaponIndex);
		OnSwitchWeaponSuccess();
	}
}

void ATPS_studyCharacter::SetWeaponIndexWithMouseWheel_Up() { SetWeaponIndexWithMouseWheel(true); }
void ATPS_studyCharacter::SetWeaponIndexWithMouseWheel_Down() { SetWeaponIndexWithMouseWheel(false); }

void ATPS_studyCharacter::SetWeaponIndexWithNumpad(int numberInput)
{

	if (numberInput >= WeaponNames.Num()) { return; }

	LastWeaponIndex = WeaponIndex;

	if ((WeaponNames.Num() > WeaponIndex) && IsAbleToSwitchWeapon()) 
	{
		WeaponIndex = numberInput;
		SetWeaponMode(WeaponIndex);
		OnSwitchWeaponSuccess();
	}
}

void ATPS_studyCharacter::SetWeaponIndexWithNumpad_1() { SetWeaponIndexWithNumpad(0); }
void ATPS_studyCharacter::SetWeaponIndexWithNumpad_2() { SetWeaponIndexWithNumpad(1); }
void ATPS_studyCharacter::SetWeaponIndexWithNumpad_3() { SetWeaponIndexWithNumpad(2); }
void ATPS_studyCharacter::SetWeaponIndexWithNumpad_4() { SetWeaponIndexWithNumpad(3); }

// 4.z SWITCH WEAPON

void ATPS_studyCharacter::SwitchWeaponMesh() {
	USkeletalMeshComponent* WeaponMesh = GetMesh(); // change it to accept additional weapon mesh later
	WeaponInWorld = Cast<USceneComponent>(WeaponMesh);
}

// 5.a PICKUP
void ATPS_studyCharacter::AddAmmo(int addAmmo, EAmmoType ammoType) {
	switch (ammoType) {
	case EAmmoType::StandardAmmo:
		Ammunition.StandardAmmo += addAmmo;
		break;
	case EAmmoType::RifleAmmo:
		Ammunition.RifleAmmo += addAmmo;
		break;
	case EAmmoType::ShotgunAmmo:
		Ammunition.ShotgunAmmo += addAmmo;
		break;
	case EAmmoType::Rocket:
		Ammunition.Rocket += addAmmo;
		break;
	case EAmmoType::Arrow:
		Ammunition.Arrow += addAmmo;
		break;
	case EAmmoType::Grenade:
		Ammunition.Grenade += addAmmo;
		break;
	case EAmmoType::Mine:
		Ammunition.Mine += addAmmo;
	}
}
// 5.z PICKUP

void ATPS_studyCharacter::SetIsTransitioningAiming(bool isTransitioningAiming) 
{
	bIsTransitioningAiming = isTransitioningAiming;
}

bool ATPS_studyCharacter::GetTransitioningAiming() const { return bIsTransitioningAiming; }

float ATPS_studyCharacter::AssignNormalizedVelo(float MyValue, bool bOtherButtonPressed) 
{
	FVector myVelo = GetVelocity();
	float mySpeed = FVector(myVelo.X, myVelo.Y, 0.0f).Size();
	float divider = (bOtherButtonPressed) ? UKismetMathLibrary::Sqrt(2.0f) : 1.0f;
	return (mySpeed * MyValue) / (divider * GetCharacterMovement()->MaxWalkSpeed);
}

bool ATPS_studyCharacter::CheckAndCallRunOutOfAmmo(int ammo) 
{
	if (ammo <= 0) { OnWeaponRunOutOfAmmo(); }
	return ammo > 0;
}

bool ATPS_studyCharacter::IsAmmoEnough(EAmmoType ammo) 
{
	switch (ammo) 
	{
	case EAmmoType::StandardAmmo:
		return CheckAndCallRunOutOfAmmo(Ammunition.StandardAmmo);
	case EAmmoType::RifleAmmo:
		return CheckAndCallRunOutOfAmmo(Ammunition.RifleAmmo);
	case EAmmoType::ShotgunAmmo:
		return CheckAndCallRunOutOfAmmo(Ammunition.ShotgunAmmo);
	case EAmmoType::Rocket:
		return CheckAndCallRunOutOfAmmo(Ammunition.Rocket);
	case EAmmoType::Arrow:
		return CheckAndCallRunOutOfAmmo(Ammunition.Arrow);
	case EAmmoType::Grenade:
		return CheckAndCallRunOutOfAmmo(Ammunition.Grenade);
	case EAmmoType::Mine:
		return CheckAndCallRunOutOfAmmo(Ammunition.Mine);
	default:
		return false;
	}
}

bool ATPS_studyCharacter::IsEnergyEnough(EEnergyType EnergyType) 
{
	switch (CurrentWeapon.EnergyType) 
	{
	case EEnergyType::MP:
		return CharacterStat.MP >= CurrentWeapon.EnergyUsePerShot;
	case EEnergyType::Fuel:
		return EnergyExternal.Fuel >= CurrentWeapon.EnergyUsePerShot;
	case EEnergyType::Battery:
		return EnergyExternal.Battery >= CurrentWeapon.EnergyUsePerShot;
	case EEnergyType::Overheat:
		return EnergyExternal.Overheat < 100.0f;
	default:
		return false;
	}
}