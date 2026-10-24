#pragma once
#include <functional>
#include <utility>
#include "Core/Types.h"
#include "Containers/Array.h"


// Multicast 할시 등록 핸들 ID 발급
struct FDelegateHandle
{
	uint64 ID = 0;

	bool IsValid() const { return ID != 0; }
	void Reset() { ID = 0; }

	bool operator==(const FDelegateHandle& Other) const
	{
		return ID == Other.ID;
	}

	static FDelegateHandle GetHandle()
	{
		FDelegateHandle newHandle;
		newHandle.ID = NextHandleID;
		NextHandleID++;

		return newHandle;
	}

	inline static uint64 NextHandleID = 1;
};

// Singlecast
template<typename... Args>
class TDelegate
{
public:
	using Callback = std::function<void(Args...)>;

	// 기존 바인딩을 새로운 콜백으로 교체
	void Bind(Callback InFunc)
	{
		func = std::move(InFunc);
	}

	// 객체 수명은 사용자가 관리
	template<typename T>
	void BindMember(T* instance, void (T::* func)(Args...))
	{
		if (!instance || !func)
		{
			Unbind();
			return;
		}

		Bind(
			[instance, func](Args... args)
			{
				(instance->*func)(std::forward<Args>(args)...);
			}
		);
	}

	bool IsBound() const
	{
		return static_cast<bool>(func);
	}

	// 객체 파괴 전에 반드시 해제해야 한다
	void Unbind()
	{
		func = nullptr;
	}

	// 바인딩되어 있다는 전제
	void Execute(Args... args)
	{
		assert(IsBound() && "Delegate is not bound");

		func(std::forward<Args>(args)...);
	}

	
	// 바인딩되어 있을 때만 실행
	// 주의: BindMember에 등록한 객체가 파괴되었는지는 감지하지 못한다.
	bool ExecuteIfBound(Args... args)
	{
		if (!IsBound())
		{
			return false;
		}

		func(std::forward<Args>(args)...);
		return true;
	}

private:
	Callback func;
};

// Multicast
template<typename... Args>
class TMulticastDelegate
{
public:
	TMulticastDelegate() = default;

	TMulticastDelegate(const TMulticastDelegate& Other) = delete;
	TMulticastDelegate& operator=(const TMulticastDelegate& Other) = delete;

	using Callback = std::function<void(Args...)>;

	// 새 핸들 생성 => 핸들과 콜백 저장 => 핸들 반환
	// 일반 함수나 람다 등록
	FDelegateHandle Add(Callback func, const void* InOwner)
	{
		assert(InOwner && "Owner Required!");

		if (!func)
		{
			return {};
		}

		auto Entry = MakeUnique<FDelegateEntry>();

		Entry->Handle = FDelegateHandle::GetHandle();
		Entry->Function = std::move(func);
		Entry->Owner = InOwner;

		const FDelegateHandle NewHandle = Entry->Handle;

		Functions.Add(std::move(Entry));

		return NewHandle;
	}

	// 클래스 멤버 함수 바인딩
	// 일반 C++ 객체 포인터와 멤버 함수를 등록
	// 객체가 파괴되기 전에 등록을 해제해야 한다.
	template<typename T>
	FDelegateHandle AddRaw(T* Instance, void (T::* Func)(Args...))
	{
		if (!Instance || !Func)
		{
			return {};
		}

		// 객체 포인터와 멤버 함수 포인터를 함께 보관
		// [Instance, Func] // 두 포인터를 값으로 보관
		// (Args... args)   // Broadcast가 전달하는 인자를 받음
		return Add(
			[Instance, Func](Args... args)
			{
				(Instance->*Func)(args...); // 해당 객체의 멤버 함수를 호출
			},
			Instance // FDelegateEntry.Owner로 전달
		);
	}

	// 같은 Delegate 중첩 Broadcast는 허용
	// Depth를 통해 관리한다
	// 이번 Broadcast가 시작될 때 등록되어 있던 콜백까지만 실행
	void Broadcast(Args... args)
	{
		struct FScopedBroadcast
		{
			TMulticastDelegate& Delegate;

			explicit FScopedBroadcast(TMulticastDelegate& InDelegate) : Delegate(InDelegate)
			{
				++Delegate.BroadcastDepth;
			}

			~FScopedBroadcast()
			{
				--Delegate.BroadcastDepth;

				if (Delegate.BroadcastDepth == 0)
				{
					Delegate.CompactPendingRemovals();
				}
			}

			FScopedBroadcast(const FScopedBroadcast&) = delete;
			FScopedBroadcast& operator=(const FScopedBroadcast&) = delete;
		};

		FScopedBroadcast Guard(*this);

		int32 FunctionsSize = Functions.Num();

		for (int32 i = 0; i < FunctionsSize; ++i)
		{
			FDelegateEntry* Entry = Functions[i].get();

			if (!Entry->bPendingRemove)
			{
				Entry->Function(args...);
			}
		}
	}

	bool IsBound() const
	{
		for (const auto& Entry : Functions)
		{
			if (!Entry->bPendingRemove)
			{
				return true;
			}
		}

		return false;
	}

	bool Remove(FDelegateHandle Handle)
	{
		if (!Handle.IsValid())
		{
			return false;
		}

		for (int32 i = 0; i< Functions.Num(); ++i)
		{
			if (Functions[i]->Handle == Handle && !Functions[i]->bPendingRemove)
			{
				RemoveEntryAt(i);
				return true;
			}
		}
		return false;
	}

	// 해당 Delegate에 들어간 특정객체 함수콜백을 모두 제거하고, 제거한 개수를 반환
	int32 RemoveAll(const void* Owner)
	{
		// nullptr을 대상으로 일반 콜백들이 일괄 삭제되는 것을 방지
		if (!Owner)
		{
			return 0;
		}

		int32 RemovedCount = 0;

		for (int32 i = Functions.Num(); i > 0; --i)
		{
			const int32 Index = i - 1;

			if (Functions[Index]->Owner == Owner && !Functions[Index]->bPendingRemove)
			{
				RemoveEntryAt(Index);
				++RemovedCount;
			}
		}

		return RemovedCount;
	}

	void Clear()
	{
		if (BroadcastDepth > 0)
		{
			for (const auto& Entry : Functions)
			{
				Entry->bPendingRemove = true;
			}

			return;
		}

		Functions.Reset();
	}

private:
	struct FDelegateEntry
	{
		FDelegateHandle Handle;             // 어떤 등록인지 식별
		Callback Function;					// 실행할 함수
		const void* Owner = nullptr;		// 등록한 함수의 대상 객체
		bool bPendingRemove = false;
	};

	TArray<TUniquePtr<FDelegateEntry>> Functions;
	// 현재 실행 중인 Broadcast의 중첩 깊이
	// Add는 깊이에 관계없이 허용한다.
	// 제거는 깊이가 0이면 즉시 삭제하고, 1 이상이면 삭제를 미룬다.
	int32 BroadcastDepth = 0;

	// 실행 중이면 표시만, 실행 중이 아니면 즉시 삭제
	void RemoveEntryAt(int32 Index)
	{
		if (BroadcastDepth > 0)
		{
			Functions[Index]->bPendingRemove = true;
		}
		else
		{
			Functions.RemoveAt(Index);
		}
	}

	// 가장 바깥 Broadcast가 끝난 뒤 호출
	void CompactPendingRemovals()
	{
		assert(BroadcastDepth == 0);

		for (int32 i = Functions.Num(); i > 0; --i)
		{
			const int32 Index = i - 1;

			if (Functions[Index]->bPendingRemove)
			{
				Functions.RemoveAt(Index);
			}
		}
	}
};

// Unreal 식 Delegate 선언
// 타입을 먼저 선언하고, 필요한 클래스마다 그 타입의 멤버 변수를 두기 위해
#define DECLARE_DELEGATE(Name, ...) \
    using Name = TDelegate<__VA_ARGS__>;
#define DECLARE_MULTICAST_DELEGATE(Name, ...) \
	using Name = TMulticastDelegate<__VA_ARGS__>;
