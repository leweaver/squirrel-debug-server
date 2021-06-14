import assert = require('assert');
import { Runstate, StackEntry, Status } from '../squidDto';

class Fixtures {
    // Note this is upper case, as that is what the remote service will send.
    public static runtstatePaused: string = "paused";
    public static runtstatePausing: string = "pausing";
    public static runtstateRunning: string = "running";
    public static createSerializedStackEntry(): any {
        return {
            "file": "file.txt",
            "line": 1,
            "function": "function()"
        };
    }
    public static createSerializedStatus(stackCount: number = 0): any {
        return {
            "runstate": Fixtures.runtstatePaused,
            "stack": (() => {
                let vals: any[] = [];
                for (let i = 0; i < stackCount; i++) {
                    let se = this.createSerializedStackEntry();
                    se.line = i + 1;
                    vals.push(se);
                }
                return vals;
            })(),
        };
    }
}

suite('StackEntry DTO', () => {
    test('deserialized correctly', () => {
        let fixture = Fixtures.createSerializedStackEntry();
        let dto = new StackEntry(fixture as StackEntry);
        assert.strictEqual(fixture.file, dto.file);
        assert.strictEqual(fixture.line, dto.line);
        assert.strictEqual(fixture.function, dto.function);
    });
});


suite('Status DTO', () => {
    test('stack deserialized correctly', () => {
        let stackCount = 1;
        let fixture = Fixtures.createSerializedStatus(stackCount);
        let dto = new Status(fixture as Status);
        assert.strictEqual(Runstate.paused, dto.runstate);
        assert.strictEqual(stackCount, dto.stack.length);
        assert.ok(dto instanceof Status);
        for (let i = 0; i < stackCount; i++) {
            let stackDto = dto.stack[i];
            let stackFixture = fixture.stack[i];
            assert.ok(stackDto instanceof StackEntry);
            assert.strictEqual(stackFixture.file, stackDto.file);
            assert.strictEqual(stackFixture.line, stackDto.line);
            assert.strictEqual(stackFixture.function, stackDto.function);
        }
    });

    suite('enum deserialized correctly', () => {
        let states = [
            { "runstate": Runstate.paused, "fixture": Fixtures.runtstatePaused },
            { "runstate": Runstate.pausing, "fixture": Fixtures.runtstatePausing },
            { "runstate": Runstate.running, "fixture": Fixtures.runtstateRunning }
        ];
        for (let state of states) {
            test(state.fixture, () => {
                let fixture = Fixtures.createSerializedStatus(0);
                fixture.runstate = state.fixture;
                let dto = new Status(fixture as Status);
                assert.strictEqual(state.runstate, dto.runstate);
            });
        }
    });
});