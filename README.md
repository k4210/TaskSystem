Task system + coroutines

The main idea is to write code like this:

```
[](AssetHolder in_asset, AssetHolder in_asset2) -> TDetachCoroutine
{
....
  {
    AccessScopeCo<SampleAsset*> guard = co_await in_asset;
    guard->SaveState();
  }
....
}
```

When the asset is not accessible, the thread will pick up another task. No worker thread should be ever halt, due to a synchronization.

Next steps:
- ensure at compile time, that "scope guards" are not nested.
- Distinquish read and write access.
- implement access scope guard, that will grand/sync access for multiple assets (like std::scoped_lock)
