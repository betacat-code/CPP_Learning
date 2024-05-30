# 单例模式实现区别

单例模式即，类的一个对象成为系统中的唯一实例。
- 私有化构造函数：这样外界就无法自由地创建类对象，进而阻止了多个实例的产生。
- 类定义中含有该类的唯一静态私有对象：静态变量存放在全局存储区，且是唯一的，供所有对象使用。
- 用公有的静态函数来获取该实例：提供了访问接口。

单例模式一般分为懒汉式和饿汉式。
- 懒汉式：在使用类对象（单例实例）时才会去创建它。
- 饿汉式：单例实例在类装载时构建，有可能全局都没使用过，但它占用了空间。

## 代码
```cpp
//懒汉式
class Object
{
private:
    int value;
    static Object* pobj;
private:
    Object(int x = 0) :value(x) {
        std::cout << "Create Object" << this << std::endl;
    }
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete; // C++ 11;
public:
    ~Object() { std::cout << "Destroy Object" << this << std::endl; }
    static Object* GetObject(int x){
        // 多线程下不安全 需要创建锁 
        if (nullptr == pobj){
            pobj = new Object(x);
        }
        return pobj;
    }
};
Object* Object::pobj = nullptr;
int main()
{
    Object* pobja = Object::GetObject(100);
    return  0;
}
```
在多线程下可能创建多个对象，需要在创建时加锁，保证只创建一个。

```cpp
//饿汉式
class Object
{
private:
    int value;
    static Object obj;   //静态对象  存储在数据区
private:
    Object(int x = 0) :value(x) { cout << "Create Int : " << this << endl; }
    Object(const Object& it) = delete;  //删除拷贝构造
    Object& operator=(const Object& s) = delete;//删除赋值
public:
    ~Object() { cout << "Destroy Int: " << this << endl; }
    static Object& getInt()  //静态方法访问静态成员
    {
        return Object::obj;
    }
};
Object Object::obj(10);
int main()
{
    Object& a = Object::getInt();
    return 0;
}

```

# 策略模式

策略模式即，一个类的行为或其算法可以在运行时更改。在策略模式中，需要创建表示各种策略的对象和一个行为随着策略对象改变而改变的 Context 对象。策略对象更改 Context 对象的执行算法。

在策略模式中，通常包括以下几个角色：
- 策略接口（Strategy Interface）： 定义了一个策略的公共接口，所有具体的策略类都需要实现这个接口。这个接口声明了策略对象将执行的操作。
- 具体策略类（Concrete Strategy Classes）： 实现了策略接口，提供了具体的算法或行为。每个具体策略类都封装了实现特定行为或算法的代码。
- 上下文（Context）： 维护一个指向策略对象的引用，并定义一个接口来让策略对象执行其算法。上下文类并不知道具体的策略类，它只知道策略接口。这样，上下文可以将请求转发给当前关联的策略对象来执行。

# 实现步骤及代码

首先，需要定义一个策略接口。这个接口通常是一个纯虚类，声明了一组公共的、需要由具体策略类来实现的方法。这些方法是策略对象将执行的行为的抽象描述。

创建实现策略接口的具体策略类。每个具体策略类都包含了实现特定算法或行为的代码。这些类继承了策略接口，并实现了接口中声明的所有方法。

上下文类负责维护对策略对象的引用，并定义了一个接口，以便客户端代码可以通过这个接口来执行策略对象的方法。上下文类通常包含一个指向策略接口的指针或引用，并通过这个指针或引用来调用策略方法。

在客户端代码中，创建具体策略类的对象，并将其传递给上下文对象。上下文对象使用这个策略对象来执行相应的算法或行为。客户端代码可以通过调用上下文类的方法来间接调用策略对象的方法。

如果需要改变行为，客户端代码可以创建另一个策略对象，并将其设置为上下文对象的新策略。这样，上下文对象在执行策略时会使用新的算法或行为。

```cpp
// 步骤1: 定义策略接口  
class Strategy {
public:
	virtual ~Strategy() {}
	virtual void execute() = 0;
};
// 步骤2: 实现具体策略类  
class ConcreteStrategyA : public Strategy {
public:
	void execute() override {
		std::cout << "Executing strategy A" << std::endl;
	}
};
class ConcreteStrategyB : public Strategy {
public:
	void execute() override {
		std::cout << "Executing strategy B" << std::endl;
	}
};
// 步骤3: 创建上下文类  
class Context {
public:
	// 通过构造函数设置策略  
	Context(std::unique_ptr<Strategy> strategy) : strategy(std::move(strategy)) {}
	// 执行策略  
	void executeStrategy() {
		if (strategy) {
			strategy->execute();
		}
		else {
			std::cout << "No strategy is set." << std::endl;
		}
	}
	// 更改策略  
	void setStrategy(std::unique_ptr<Strategy> newStrategy) {
		strategy = std::move(newStrategy);
	}

private:
	std::unique_ptr<Strategy> strategy;  
};

// 在客户端代码中设置和执行策略  
int main()
{
	// 创建具体策略对象并使用unique_ptr管理  
	auto strategyA = std::make_unique<ConcreteStrategyA>();
	auto strategyB = std::make_unique<ConcreteStrategyB>();
	// 创建上下文对象并设置初始策略  
	Context context(std::move(strategyA));
	context.executeStrategy(); // 输出：Executing strategy A  
	// 更改策略  
	context.setStrategy(std::move(strategyB));
	context.executeStrategy(); // 输出：Executing strategy B  
	// 此时strategyA和strategyB已经被unique_ptr自动释放  
	return 0;
}
```

