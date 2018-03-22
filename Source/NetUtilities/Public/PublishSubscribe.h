#pragma once

#include "CoreMinimal.h"
#include "Interface.h"
#include "GameFramework/PlayerState.h"

#include "PublishSubscribe.generated.h"

class UPublisherInterface;
class IPublisherInterface;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class USubscriberInterface
	: public UInterface
{
	GENERATED_BODY()
};

class NETUTILITIES_API ISubscriberInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Server, Reliable, WithValidation, Category = "Net Utilities|Publish/Subscribe")
	virtual void Server_Subscribe(const TScriptInterface<IPublisherInterface>& InPublisher, APlayerState* InSubscriber);
	virtual bool Server_Subscribe_Validate(TScriptInterface<IPublisherInterface> InPublisher, APlayerState* InSubscriber) { return true; }
	virtual void Server_Subscribe_Implementation(TScriptInterface<IPublisherInterface> InPublisher, APlayerState* InSubscriber) { }

	UFUNCTION(BlueprintCallable, Server, Reliable, WithValidation, Category = "Net Utilities|Publish/Subscribe")
	virtual void Server_Unsubscribe(const TScriptInterface<IPublisherInterface>& InPublisher, APlayerState* InSubscriber);
	virtual bool Server_Unsubscribe_Validate(TScriptInterface<IPublisherInterface> InPublisher, APlayerState* InSubscriber) { return true; }
	virtual void Server_Unsubscribe_Implementation(TScriptInterface<IPublisherInterface> InPublisher, APlayerState* InSubscriber) { }
};

UINTERFACE(MinimalAPI)
class UPublisherInterface
	: public UInterface
{
	GENERATED_BODY()
};

class NETUTILITIES_API IPublisherInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Net Utilities|Publish/Subscribe")
	void Subscribe(APlayerState* InSubscriber);
	virtual void Subscribe_Implementation(APlayerState* InSubscriber) { }

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Net Utilities|Publish/Subscribe")
	void Unsubscribe(APlayerState* InSubscriber);
	virtual void Unsubscribe_Implementation(APlayerState* InSubscriber) { }
};