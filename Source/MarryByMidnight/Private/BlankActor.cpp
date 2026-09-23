// Fill out your copyright notice in the Description page of Project Settings.


#include "BlankActor.h"

// Sets default values
ABlankActor::ABlankActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void ABlankActor::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ABlankActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

